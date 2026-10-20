--[[
    This file is part of mod-wow-fates.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
    Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program. If not, see <http://www.gnu.org/licenses/>.
]]

--[[----------------------------------------------------------------------------
    The Expeditions on the world map and the minimap, client side (shipped by
    AIO): each active Expedition's area, a circle, drawn as mod-forever-ui
    draws the tracked quests' areas (MinimapQuests.lua, WorldMapZoom.lua), in
    orange instead of blue (Interface\WowFates\Area, the quest area's own
    texture and alphas recoloured: tools/gen_client_data.py). Hovering an area
    shows the Expedition's name, level, objective and time left.

    Works on the game's own interface as with mod-forever-ui:
      - world map: a frame over WorldMapButton, whose size the map's fractions
        are measured on (ForeverUI's zoom enlarges it, the areas follow); while
        zoomed, each area is cropped to ForeverUI's view (W.canvas);
      - minimap: nothing clips to a circle in 3.3.5, so the minimap is cut into
        horizontal strips, each a ScrollFrame as wide as the circle at its
        narrower edge, which crops the areas it holds; drawn again as the
        player moves. A circle turns with nothing: with "Rotate Minimap" on,
        only its centre turns around the player.

    Positions: the module sends world coordinates (addon message "WowFates":
    "reset", "set" with id, map, x, y, radius, level, progress, count,
    seconds left, name, objective, and "del" with id). WowFatesBounds
    (WowFates_Bounds_Client.lua, from WorldMapArea.dbc) gives each map's edges
    in world coordinates: an Expedition's map fraction, and the player's world
    position from GetPlayerMapPosition.
------------------------------------------------------------------------------]]

local AIO = AIO or require("AIO")

if AIO.AddAddon() then
    return                                  -- server side: we stop here
end

local PREFIX = "WowFates"
local TEXTURE = "Interface\\WowFates\\Area"
local STRIPS = 24                           -- strips across the minimap's diameter
local MINIMAP_WAIT = 0.05                   -- the minimap is drawn again at most twenty times a second
local TITLE_COLOR = { 1, 0.55, 0.2 }

-- Minimap diameter in yards by zoom (0 to 5), outdoors and indoors (the client's fixed values)
local OUTDOOR = { [0] = 466 + 2 / 3, 400, 333 + 1 / 3, 266 + 2 / 3, 200, 133 + 1 / 3 }
local INDOOR = { [0] = 300, 240, 180, 120, 80, 50 }

-- ---------------------------------------------------------------------------
-- TEXTS
-- ---------------------------------------------------------------------------

local TEXTS = {
    enUS = { level = "Expedition, level %d", progress = "%s: %d/%d", left = "%d min left", soon = "Ending soon" },
    frFR = { level = "Expédition, niveau %d", progress = "%s : %d/%d", left = "%d min restantes", soon = "Bientôt terminée" },
    deDE = { level = "Expedition, Stufe %d", progress = "%s: %d/%d", left = "Noch %d Min.", soon = "Endet bald" },
    esES = { level = "Expedición, nivel %d", progress = "%s: %d/%d", left = "Quedan %d min", soon = "Termina pronto" },
    ruRU = { level = "Экспедиция, уровень %d", progress = "%s: %d/%d", left = "Осталось %d мин.", soon = "Скоро завершится" },
}
TEXTS.esMX = TEXTS.esES
local L = TEXTS[GetLocale()] or TEXTS.enUS

-- ---------------------------------------------------------------------------
-- THE EXPEDITIONS
-- ---------------------------------------------------------------------------

local expeditions = {}                      -- id -> { map, x, y, r, level, progress, count, ends, name, objective }

local function split(text)
    local out = {}
    for field in string.gmatch(text .. "\t", "([^\t]*)\t") do
        out[#out + 1] = field
    end
    return out
end

local refresh                               -- set below

local function onMessage(message)
    local f = split(message)
    local command = f[1]
    if command == "reset" then
        for k in pairs(expeditions) do expeditions[k] = nil end
    elseif command == "set" and #f >= 12 then
        local id = tonumber(f[2])
        if id then
            expeditions[id] = {
                map = tonumber(f[3]), x = tonumber(f[4]), y = tonumber(f[5]), r = tonumber(f[6]),
                level = tonumber(f[7]), progress = tonumber(f[8]), count = tonumber(f[9]),
                ends = GetTime() + (tonumber(f[10]) or 0), name = f[11], objective = f[12],
            }
        end
    elseif command == "del" then
        local id = tonumber(f[2])
        if id then expeditions[id] = nil end
    end
    refresh()
end

-- ---------------------------------------------------------------------------
-- MAP GEOMETRY
-- ---------------------------------------------------------------------------

-- The map shown (the player's while the world map is closed): { map, left, right, top, bottom }
local function currentBounds()
    if not (WowFatesBounds and GetCurrentMapAreaID) then return nil end
    if GetCurrentMapDungeonLevel and (GetCurrentMapDungeonLevel() or 0) > 0 then return nil end
    return WowFatesBounds[GetCurrentMapAreaID()]
end

-- An Expedition on the map: its centre in fractions and its radius in fractions of the width
-- and the height
local function onMap(e, b)
    if not (b and e.map == b[1] and e.x and e.y and e.r) then return nil end
    local w, h = b[2] - b[3], b[4] - b[5]
    local fx, fy = (b[2] - e.y) / w, (b[4] - e.x) / h
    local rx, ry = e.r / w, e.r / h
    if fx + rx < 0 or fx - rx > 1 or fy + ry < 0 or fy - ry > 1 then return nil end
    return fx, fy, rx, ry
end

-- The player's world position, from the map shown (nil when he is not on it)
local function playerPosition(b)
    if not b then return nil end
    local px, py = GetPlayerMapPosition("player")
    if not px or (px == 0 and py == 0) then return nil end
    return b[4] - py * (b[4] - b[5]), b[2] - px * (b[2] - b[3])
end

local function tooltipLines(tooltip, e)
    tooltip:AddLine(e.name or "", TITLE_COLOR[1], TITLE_COLOR[2], TITLE_COLOR[3])
    tooltip:AddLine(string.format(L.level, e.level or 0), 1, 0.82, 0)
    if e.objective and e.objective ~= "" then
        tooltip:AddLine(string.format(L.progress, e.objective, e.progress or 0, e.count or 0), 1, 1, 1, true)
    end
    local left = math.ceil(((e.ends or 0) - GetTime()) / 60)
    if left >= 1 then
        tooltip:AddLine(string.format(L.left, left), 0.7, 0.7, 0.7)
    else
        tooltip:AddLine(L.soon, 0.7, 0.7, 0.7)
    end
end

local function showTooltip(tooltip, owner, list)
    tooltip:SetOwner(owner, "ANCHOR_CURSOR")
    for i, e in ipairs(list) do
        if i > 1 then tooltip:AddLine(" ") end
        tooltipLines(tooltip, e)
    end
    tooltip:Show()
end

-- ---------------------------------------------------------------------------
-- WORLD MAP
-- ---------------------------------------------------------------------------

local world = { textures = {}, hovered = false }

local function worldTooltip()
    return WorldMapTooltip or GameTooltip
end

-- ForeverUI's view while its map is zoomed (the enlarged map spills past it), else the map
local function worldClip()
    local W = ForeverUI and ForeverUI.WorldMap
    if W and W.zoom and W.zoom.active and W.canvas and W.canvas:IsVisible() then
        return W.canvas
    end
    return WorldMapButton
end

local function buildWorld()
    if world.frame or not WorldMapButton then return world.frame end
    -- two anchors: ForeverUI's zoom moves the markers anchored by one point only
    local f = CreateFrame("Frame", nil, WorldMapButton)
    f:SetPoint("TOPLEFT", WorldMapButton, "TOPLEFT", 0, 0)
    f:SetPoint("BOTTOMRIGHT", WorldMapButton, "BOTTOMRIGHT", 0, 0)
    -- above the map's art, under its markers, where the quest areas draw
    f:SetFrameLevel(WorldMapBlobFrame and WorldMapBlobFrame:GetFrameLevel() or WorldMapButton:GetFrameLevel())
    world.frame = f
    return f
end

local function worldTexture(i)
    local t = world.textures[i]
    if not t then
        t = world.frame:CreateTexture(nil, "ARTWORK")
        t:SetTexture(TEXTURE)
        world.textures[i] = t
    end
    return t
end

-- Lays the areas out on the map as it is now, cropped to the view; returns the shown ones, for
-- the hover test: { e, cx, cy, rw, rh } in the frame's units, y downward
local placed = {}

local function layoutWorld()
    local f = world.frame
    for k in pairs(placed) do placed[k] = nil end
    local used = 0
    local b = currentBounds()
    local W, H = f:GetWidth(), f:GetHeight()
    local clip = worldClip()
    local fs = f:GetEffectiveScale()
    local left, top = f:GetLeft(), f:GetTop()
    local cl, cr, ct, cb = clip:GetLeft(), clip:GetRight(), clip:GetTop(), clip:GetBottom()
    if b and W and W > 0 and left and cl then
        local cs = clip:GetEffectiveScale()
        -- the view in the frame's units, y downward from its top
        local vl, vr = cl * cs / fs - left, cr * cs / fs - left
        local vt, vb = top - ct * cs / fs, top - cb * cs / fs
        for _, e in pairs(expeditions) do
            local fx, fy, rx, ry = onMap(e, b)
            if fx then
                local cx, cy, rw, rh = fx * W, fy * H, rx * W, ry * H
                local l, r, t, d = cx - rw, cx + rw, cy - rh, cy + rh
                local il, ir, it, id = math.max(l, vl), math.min(r, vr), math.max(t, vt), math.min(d, vb)
                if ir > il and id > it then
                    used = used + 1
                    local tex = worldTexture(used)
                    tex:ClearAllPoints()
                    tex:SetPoint("TOPLEFT", f, "TOPLEFT", il, -it)
                    tex:SetWidth(ir - il)
                    tex:SetHeight(id - it)
                    tex:SetTexCoord((il - l) / (r - l), (ir - l) / (r - l), (it - t) / (d - t), (id - t) / (d - t))
                    tex:Show()
                    placed[#placed + 1] = { e = e, cx = cx, cy = cy, rw = rw, rh = rh,
                                            vl = vl, vr = vr, vt = vt, vb = vb }
                end
            end
        end
    end
    for i = used + 1, #world.textures do
        world.textures[i]:Hide()
    end
end

local function hoverWorld()
    local f = world.frame
    local tooltip = worldTooltip()
    local found = {}
    if #placed > 0 and f:IsVisible() and MouseIsOver(f) then
        local x, y = GetCursorPosition()
        local s = f:GetEffectiveScale()
        x, y = x / s - f:GetLeft(), f:GetTop() - y / s
        for _, p in ipairs(placed) do
            local dx, dy = (x - p.cx) / p.rw, (y - p.cy) / p.rh
            if dx * dx + dy * dy <= 1 and x >= p.vl and x <= p.vr and y >= p.vt and y <= p.vb then
                found[#found + 1] = p.e
            end
        end
    end
    if #found > 0 then
        showTooltip(tooltip, f, found)
        world.hovered = true
    elseif world.hovered then
        world.hovered = false
        if tooltip:GetOwner() == f then tooltip:Hide() end
    end
end

-- ---------------------------------------------------------------------------
-- MINIMAP
-- ---------------------------------------------------------------------------

local mini = { strips = {}, hovered = false, wait = 0, drawn = {} }

local function unitsPerYard()
    local zoom = Minimap:GetZoom() or 0
    local indoors = IsIndoors and IsIndoors()
    local diameter = (indoors and INDOOR or OUTDOOR)[zoom] or OUTDOOR[0]
    return Minimap:GetWidth() / diameter
end

local function rotating()
    return GetCVar and GetCVar("rotateMinimap") == "1"
end

local function buildMinimap()
    if mini.holder or not Minimap then return mini.holder end
    local holder = CreateFrame("Frame", nil, Minimap)
    holder:SetAllPoints(Minimap)
    holder:SetFrameLevel(Minimap:GetFrameLevel() + 1)
    for i = 1, STRIPS do
        local clip = CreateFrame("ScrollFrame", nil, holder)
        local child = CreateFrame("Frame", nil, clip)
        clip:SetScrollChild(child)
        clip:EnableMouse(false)
        child:EnableMouse(false)
        mini.strips[i] = { clip = clip, child = child, textures = {} }
    end
    mini.holder = holder
    return holder
end

-- Each strip as wide as the circle at its narrower edge
local function layoutStrips(D)
    if mini.diameter == D then return end
    mini.diameter = D
    local R, h = D / 2, D / STRIPS
    for i, s in ipairs(mini.strips) do
        local top = (i - 1) * h
        local dy = math.max(math.abs(top - R), math.abs(top + h - R))
        local half = dy < R and math.sqrt(R * R - dy * dy) or 0
        s.top, s.half, s.height = top, half, h
        s.clip:ClearAllPoints()
        s.clip:SetPoint("TOPLEFT", mini.holder, "TOPLEFT", R - half, -top)
        s.clip:SetWidth(math.max(half * 2, 0.01))
        s.clip:SetHeight(h)
        s.child:SetWidth(math.max(half * 2, 0.01))
        s.child:SetHeight(h)
    end
end

local function stripTexture(s, i)
    local t = s.textures[i]
    if not t then
        t = s.child:CreateTexture(nil, "ARTWORK")
        t:SetTexture(TEXTURE)
        s.textures[i] = t
    end
    return t
end

-- The areas on the minimap now: { e, cx, cy, r } in the minimap's units from its top left, y
-- downward
local function minimapAreas()
    if WorldMapFrame and WorldMapFrame:IsShown() then
        -- the map shown may not be the player's: keep the last drawing
        return mini.kept
    end
    local list = mini.drawn
    for k in pairs(list) do list[k] = nil end
    local b = currentBounds()
    local wx, wy = playerPosition(b)
    if not wx then return list end
    local D = Minimap:GetWidth()
    local R, u = D / 2, unitsPerYard()
    local facing = rotating() and GetPlayerFacing and GetPlayerFacing() or nil
    for _, e in pairs(expeditions) do
        if e.map == b[1] and e.x then
            -- world x is north, world y is west
            local east, north = wy - e.y, e.x - wx
            local sx, sy = east, north
            if facing then
                local c, s = math.cos(facing), math.sin(facing)
                sx, sy = east * c + north * s, -east * s + north * c
            end
            local cx, cy, r = R + sx * u, R - sy * u, e.r * u
            -- only those that reach the minimap
            local reach = math.sqrt((cx - R) ^ 2 + (cy - R) ^ 2)
            if reach < R + r then
                list[#list + 1] = { e = e, cx = cx, cy = cy, r = r }
            end
        end
    end
    mini.kept = list
    return list
end

local function drawMinimap()
    local D = Minimap:GetWidth()
    if not D or D <= 0 then return end
    layoutStrips(D)
    local R = D / 2
    local areas = minimapAreas() or {}
    for _, s in ipairs(mini.strips) do
        local used = 0
        if s.half > 0 then
            local x0 = R - s.half
            for _, a in ipairs(areas) do
                -- does the circle cross this strip?
                if a.cy + a.r > s.top and a.cy - a.r < s.top + s.height then
                    used = used + 1
                    local t = stripTexture(s, used)
                    t:ClearAllPoints()
                    t:SetPoint("CENTER", s.child, "TOPLEFT", a.cx - x0, -(a.cy - s.top))
                    t:SetWidth(a.r * 2)
                    t:SetHeight(a.r * 2)
                    t:Show()
                end
            end
        end
        for i = used + 1, #s.textures do
            s.textures[i]:Hide()
        end
    end
    mini.areas = areas
end

local function hoverMinimap()
    local found = {}
    local areas = mini.areas
    if areas and #areas > 0 and Minimap:IsVisible() and MouseIsOver(Minimap) then
        local x, y = GetCursorPosition()
        local s = Minimap:GetEffectiveScale()
        x, y = x / s - Minimap:GetLeft(), Minimap:GetTop() - y / s
        local R = Minimap:GetWidth() / 2
        if (x - R) ^ 2 + (y - R) ^ 2 <= R * R then
            for _, a in ipairs(areas) do
                if (x - a.cx) ^ 2 + (y - a.cy) ^ 2 <= a.r * a.r then
                    found[#found + 1] = a.e
                end
            end
        end
    end
    if #found > 0 then
        showTooltip(GameTooltip, Minimap, found)
        mini.hovered = true
    elseif mini.hovered then
        mini.hovered = false
        if GameTooltip:GetOwner() == Minimap then GameTooltip:Hide() end
    end
end

-- ---------------------------------------------------------------------------
-- UPDATES
-- ---------------------------------------------------------------------------

function refresh()
    if buildWorld() and world.frame:IsVisible() then layoutWorld() end
    if buildMinimap() then drawMinimap() end
end

local driver = CreateFrame("Frame")

driver:SetScript("OnUpdate", function(_, elapsed)
    -- the world map: every frame while it shows (its zoom and drag move the areas)
    if world.frame and world.frame:IsVisible() then
        layoutWorld()
        hoverWorld()
    end
    -- the minimap: as the player moves and turns
    if mini.holder then
        mini.wait = mini.wait - elapsed
        if mini.wait <= 0 then
            mini.wait = MINIMAP_WAIT
            if next(expeditions) then
                drawMinimap()
            elseif mini.areas and #mini.areas > 0 then
                drawMinimap()
            end
        end
        hoverMinimap()
    end
end)

driver:RegisterEvent("CHAT_MSG_ADDON")
driver:RegisterEvent("PLAYER_ENTERING_WORLD")
driver:RegisterEvent("ZONE_CHANGED_NEW_AREA")
driver:RegisterEvent("WORLD_MAP_UPDATE")
driver:RegisterEvent("MINIMAP_UPDATE_ZOOM")

driver:SetScript("OnEvent", function(_, event, prefix, message, channel, sender)
    if event == "CHAT_MSG_ADDON" then
        -- only the server's: a whisper of the player to himself
        if prefix == PREFIX and channel == "WHISPER" and sender == UnitName("player") then
            onMessage(message)
        end
        return
    end
    if event == "ZONE_CHANGED_NEW_AREA" and not (WorldMapFrame and WorldMapFrame:IsShown()) then
        SetMapToCurrentZone()
    end
    refresh()
end)

-- The list sent at login may have come before this code: ask for it.
if SendAddonMessage then
    SendAddonMessage(PREFIX, "hello", "WHISPER", UnitName("player"))
end
refresh()
