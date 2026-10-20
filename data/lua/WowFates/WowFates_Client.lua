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

    The Expedition the player stands in also gets, as the modern client's
    Mythic+ block (ScenarioObjectiveTrackerChallengeModeMixin), a block at the
    top of the objective tracker: its name and level, the time left, large,
    over its bar, the objective with its count and a bar in percent (the
    "enemy forces"), and whether the player took part enough to be rewarded.
    Its art is the modern client's, cut out by tools/gen_client_data.py
    (Interface\WowFates\Tracker), the timer turned orange. It is a handler of
    WatchFrame (WATCHFRAME_OBJECTIVEHANDLERS, first): mod-forever-ui keeps
    that API, so the block sits above its quests, as above the game's own.
    Entering and leaving an area, and the end, show a banner in the middle of
    the screen with a sound. The Synced debuff's tooltip gets the level and
    the share of power kept.

    Messages (addon message "WowFates", from the module): "reset"; "set" with
    id, map, x, y, radius, level, progress, count, seconds left, name,
    objective, tracker line, seconds it waits for players, type of objective (1 extermination,
    2 assault and boss, 3 gathering, 4 defence, 5 invocation, 6 wave support:
    named in the map's tooltip), 1 when players are at it ("in progress" on the map); "del" with id; "enter" and "leave"
    with id; "end" with id and 1 or 0; "me" with id, kills and participation
    (0 none, 1 not enough yet, 2 enough); "boss" with id and the leader's name,
    "defend" with id and the ally's name, "invoke" with id and the altar's
    name, "wave" with id and its number (a banner each); "sync" with level and percent kept.
    Positions are world coordinates. WowFatesBounds
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
local TRACKER = "Interface\\WowFates\\Tracker"
local SYNCED_SPELL = 96000
local LINGER = 10                           -- seconds the block stays after leaving or the end
local STRIPS = 24                           -- strips across the minimap's diameter
local MINIMAP_WAIT = 0.05                   -- the minimap is drawn again at most twenty times a second
local TITLE_COLOR = { 1, 0.55, 0.2 }
-- The banners' sounds: a boss out or a defence begun, and each wave (the ogres' war drums;
-- any sound file of the client may take their place)
local ALERT_SOUND = "Sound\\Interface\\RaidWarning.wav"
local WAVE_SOUND = "Sound\\Event Sounds\\Event_wardrum_ogre.wav"

-- Minimap diameter in yards by zoom (0 to 5), outdoors and indoors (the client's fixed values)
local OUTDOOR = { [0] = 466 + 2 / 3, 400, 333 + 1 / 3, 266 + 2 / 3, 200, 133 + 1 / 3 }
local INDOOR = { [0] = 300, 240, 180, 120, 80, 50 }

-- ---------------------------------------------------------------------------
-- TEXTS
-- ---------------------------------------------------------------------------

local TEXTS = {
    enUS = { level = "Expedition, level %d", inProgress = "In progress: %d%%", soon = "Expires soon" },
    frFR = { level = "Expédition, niveau %d", inProgress = "En cours : %d %%", soon = "Expire bientôt" },
    deDE = { level = "Expedition, Stufe %d", inProgress = "Im Gange: %d %%", soon = "Läuft bald ab" },
    esES = { level = "Expedición, nivel %d", inProgress = "En curso: %d %%", soon = "Expira pronto" },
    ruRU = { level = "Экспедиция, уровень %d", inProgress = "Выполняется: %d%%", soon = "Скоро исчезнет" },
}
local TRACKER_TEXTS = {
    enUS = {
        title = "%s - Level %d", banner = "Expedition: %s", bannerLevel = "Level %d",
        leaving = "You leave the Expedition", success = "Expedition complete", failed = "Expedition failed",
        part0 = "Participation: none", part1 = "Participation: light", part2 = "Participation: medium", part3 = "Participation: strong",
        expected = "Expected rewards:", noReward = "No reward", fullRewards = "Rewards:", bravo = "Well done!", earned = "You earned:", done = "Expedition complete", xp = "%d XP",
        kills = "Your kills: %d", types = { "Extermination", "Assault and boss", "Gathering", "Defence", "Invocation", "Wave support" }, boss = "%s appears!", invoke = "%s: channel!", defend = "Protect %s!", wave = "Wave %d", synced = "Synced to level %d: you keep %d%% of your power.",
    },
    frFR = {
        title = "%s - Niveau %d", banner = "Expédition : %s", bannerLevel = "Niveau %d",
        leaving = "Vous quittez l'Expédition", success = "Expédition accomplie", failed = "Expédition échouée",
        part0 = "Participation : aucune", part1 = "Participation : légère", part2 = "Participation : moyenne", part3 = "Participation : forte",
        expected = "Récompenses attendues :", noReward = "Aucune récompense", fullRewards = "Récompenses :", bravo = "Bravo !", earned = "Vous avez gagné :", done = "Expédition terminée", xp = "%d XP",
        kills = "Vos victimes : %d", types = { "Extermination", "Assaut et chef", "Collecte", "Défense", "Invocation", "Soutien de vagues" }, boss = "%s apparaît !", invoke = "%s : canalisez !", defend = "Protégez %s !", wave = "Vague %d", synced = "Synchronisé au niveau %d : vous gardez %d %% de votre puissance.",
    },
    deDE = {
        title = "%s - Stufe %d", banner = "Expedition: %s", bannerLevel = "Stufe %d",
        leaving = "Ihr verlasst die Expedition", success = "Expedition abgeschlossen", failed = "Expedition gescheitert",
        part0 = "Teilnahme: keine", part1 = "Teilnahme: gering", part2 = "Teilnahme: mittel", part3 = "Teilnahme: stark",
        expected = "Erwartete Belohnungen:", noReward = "Keine Belohnung", fullRewards = "Belohnungen:", bravo = "Gut gemacht!", earned = "Ihr habt erhalten:", done = "Expedition abgeschlossen", xp = "%d EP",
        kills = "Eure Siege: %d", types = { "Vernichtung", "Angriff und Anführer", "Sammeln", "Verteidigung", "Beschwörung", "Wellenabwehr" }, boss = "%s erscheint!", invoke = "%s: kanalisiert!", defend = "Beschützt %s!", wave = "Welle %d", synced = "Auf Stufe %d synchronisiert: Ihr behaltet %d %% Eurer Macht.",
    },
    esES = {
        title = "%s - Nivel %d", banner = "Expedición: %s", bannerLevel = "Nivel %d",
        leaving = "Sales de la Expedición", success = "Expedición completada", failed = "Expedición fracasada",
        part0 = "Participación: ninguna", part1 = "Participación: leve", part2 = "Participación: media", part3 = "Participación: fuerte",
        expected = "Recompensas esperadas:", noReward = "Ninguna recompensa", fullRewards = "Recompensas:", bravo = "¡Bien hecho!", earned = "Has ganado:", done = "Expedición terminada", xp = "%d de experiencia",
        kills = "Tus víctimas: %d", types = { "Exterminio", "Asalto y líder", "Recolección", "Defensa", "Invocación", "Apoyo contra oleadas" }, boss = "¡%s aparece!", invoke = "%s: ¡canaliza!", defend = "¡Protege a %s!", wave = "Oleada %d", synced = "Sincronizado al nivel %d: conservas el %d %% de tu poder.",
    },
    ruRU = {
        title = "%s - уровень %d", banner = "Экспедиция: %s", bannerLevel = "Уровень %d",
        leaving = "Вы покидаете экспедицию", success = "Экспедиция завершена", failed = "Экспедиция провалена",
        part0 = "Участие: нет", part1 = "Участие: слабое", part2 = "Участие: среднее", part3 = "Участие: сильное",
        expected = "Ожидаемые награды:", noReward = "Нет награды", fullRewards = "Награды:", bravo = "Отлично!", earned = "Вы получили:", done = "Экспедиция завершена", xp = "%d ед. опыта",
        kills = "Ваши победы: %d", types = { "Истребление", "Штурм и главарь", "Сбор", "Оборона", "Призыв", "Отражение волн" }, boss = "Появляется %s!", invoke = "%s: поддерживайте ритуал!", defend = "Защитите: %s!", wave = "Волна %d", synced = "Синхронизация с уровнем %d: у вас остается %d%% силы.",
    },
}
for locale, t in pairs(TRACKER_TEXTS) do
    for k, v in pairs(t) do TEXTS[locale][k] = v end
end
TEXTS.esMX = TEXTS.esES
local L = TEXTS[GetLocale()] or TEXTS.enUS

-- ---------------------------------------------------------------------------
-- THE EXPEDITIONS
-- ---------------------------------------------------------------------------

local expeditions = {}                      -- id -> { map, x, y, r, level, progress, count, ends, name, objective }
local rewards = {}                          -- id -> { money, xp, items = { entry, count, hidden, single } }, strong

local function split(text)
    local out = {}
    for field in string.gmatch(text .. "\t", "([^\t]*)\t") do
        out[#out + 1] = field
    end
    return out
end

local refresh                               -- set below
local track = {}                            -- the tracker, the banner and the debuff (below)

local function onMessage(message)
    local f = split(message)
    local command = f[1]
    local id = tonumber(f[2])
    if command == "reset" then
        for k in pairs(expeditions) do expeditions[k] = nil end
        for k in pairs(rewards) do rewards[k] = nil end
    elseif command == "set" and #f >= 12 and id then
        expeditions[id] = {
            id = id, map = tonumber(f[3]), x = tonumber(f[4]), y = tonumber(f[5]), r = tonumber(f[6]),
            level = tonumber(f[7]), progress = tonumber(f[8]), count = tonumber(f[9]),
            ends = GetTime() + (tonumber(f[10]) or 0), name = f[11], objective = f[12],
            label = f[13], duration = tonumber(f[14]), type = tonumber(f[15]) or 1, inProgress = f[16] == "1",
        }
        track.update(expeditions[id])
    elseif command == "del" and id then
        expeditions[id] = nil
        rewards[id] = nil
    elseif command == "enter" and id then
        track.enter(id)
    elseif command == "leave" and id then
        track.leave(id)
    elseif command == "end" and id then
        track.finish(id, f[3] == "1")
    elseif command == "me" and id then
        track.status(id, tonumber(f[3]) or 0, tonumber(f[4]) or 0, tonumber(f[5]) or 0)
    elseif command == "rew" and id then
        local items = {}
        for entry, count, hidden, single in string.gmatch(f[5] or "", "(%d+),(%d+),(%d),(%d)") do
            items[#items + 1] = { entry = tonumber(entry), count = tonumber(count), hidden = hidden == "1",
                                  single = single == "1" }
        end
        rewards[id] = { money = tonumber(f[3]) or 0, xp = tonumber(f[4]) or 0, items = items }
    elseif command == "won" and id then
        local items = {}
        for entry, count in string.gmatch(f[5] or "", "(%d+),(%d+)") do
            items[#items + 1] = { entry = tonumber(entry), count = tonumber(count) }
        end
        track.won(id, tonumber(f[3]) or 0, tonumber(f[4]) or 0, items)
    elseif command == "boss" and id then
        track.alert(id, string.format(L.boss, f[3] or ""))
    elseif command == "defend" and id then
        track.alert(id, string.format(L.defend, f[3] or ""))
    elseif command == "invoke" and id then
        track.alert(id, string.format(L.invoke, f[3] or ""))
    elseif command == "wave" and id then
        track.alert(id, string.format(L.wave, tonumber(f[3]) or 0), WAVE_SOUND)
    elseif command == "sync" then
        track.sync(tonumber(f[2]) or 0, tonumber(f[3]) or 100)
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

-- ---------------------------------------------------------------------------
-- REWARDS
-- ---------------------------------------------------------------------------

local QUESTION = "Interface\\Icons\\INV_Misc_QuestionMark"
local TIER_COLOR = {
    [0] = { 0.6, 0.6, 0.6 }, [1] = { 1, 0.5, 0.25 }, [2] = { 1, 0.82, 0 }, [3] = { 0.1, 1, 0.1 },
}

-- Asks the client for an item it has not seen, so that its name and icon come
local scan
local function request(entry)
    if not scan then
        scan = CreateFrame("GameTooltip", "WowFatesScanTooltip", nil, "GameTooltipTemplate")
        scan:SetOwner(WorldFrame or UIParent, "ANCHOR_NONE")
    end
    scan:SetHyperlink("item:" .. entry .. ":0:0:0:0:0:0:0")
end

local function itemInfo(entry)
    local name, _, quality, _, _, _, _, _, _, icon = GetItemInfo(entry)
    if not name then request(entry) end
    icon = icon or (GetItemIcon and GetItemIcon(entry)) or QUESTION
    return name, icon, quality
end

local function coins(copper)
    copper = math.floor(copper or 0)
    local g, sv, c = math.floor(copper / 10000), math.floor(copper % 10000 / 100), copper % 100
    local parts = {}
    if g > 0 then parts[#parts + 1] = g .. "|TInterface\\MoneyFrame\\UI-GoldIcon:12:12:2:0|t" end
    if sv > 0 then parts[#parts + 1] = sv .. "|TInterface\\MoneyFrame\\UI-SilverIcon:12:12:2:0|t" end
    if c > 0 or #parts == 0 then parts[#parts + 1] = c .. "|TInterface\\MoneyFrame\\UI-CopperIcon:12:12:2:0|t" end
    return table.concat(parts, " ")
end

-- What a participation gives out of the strong rewards: the share of each amount, rounded up;
-- a single item to a strong participation only. Items: { entry, count, hidden, granted }
local function shareOf(rw, tier, pct)
    local out = { money = 0, xp = 0, items = {} }
    if not rw then return out end
    if tier > 0 then
        out.money = math.floor((rw.money * pct + 99) / 100)
        out.xp = math.floor((rw.xp * pct + 99) / 100)
    end
    for _, it in ipairs(rw.items) do
        local granted = tier > 0 and not (it.single and tier < 3)
        local count = it.single and 1 or math.floor((it.count * pct + 99) / 100)
        out.items[#out.items + 1] = { entry = it.entry, count = granted and count or it.count,
                                      hidden = it.hidden, granted = granted }
    end
    return out
end

local function rewardLines(tooltip, rw)
    if not rw or (rw.money == 0 and rw.xp == 0 and #rw.items == 0) then return end
    tooltip:AddLine(L.fullRewards, 1, 0.82, 0)
    local amounts = {}
    if rw.money > 0 then amounts[#amounts + 1] = coins(rw.money) end
    if rw.xp > 0 then amounts[#amounts + 1] = string.format(L.xp, rw.xp) end
    if #amounts > 0 then tooltip:AddLine(table.concat(amounts, "   "), 1, 1, 1) end
    for _, it in ipairs(rw.items) do
        if it.hidden then
            tooltip:AddLine("|T" .. QUESTION .. ":16:16:0:0|t |cffff2020???|r")
        else
            local name, icon, quality = itemInfo(it.entry)
            local r, g, b = 1, 1, 1
            if quality and GetItemQualityColor then r, g, b = GetItemQualityColor(quality) end
            tooltip:AddLine(string.format("|T%s:16:16:0:0|t %s%s", icon, name or ("#" .. it.entry),
                it.count > 1 and (" x" .. it.count) or ""), r, g, b)
        end
    end
end

local function tooltipLines(tooltip, e)
    tooltip:AddLine(e.name or "", TITLE_COLOR[1], TITLE_COLOR[2], TITLE_COLOR[3])
    tooltip:AddLine(string.format(L.level, e.level or 0), 1, 0.82, 0)
    if L.types[e.type or 1] then tooltip:AddLine(L.types[e.type or 1], 0.9, 0.9, 0.9) end
    if e.objective and e.objective ~= "" then
        tooltip:AddLine(e.objective, 1, 1, 1, true)
    end
    -- no clock: who is at it and how far, or that it will soon go for lack of players
    if e.inProgress then
        tooltip:AddLine(string.format(L.inProgress, e.progress or 0), 0.1, 1, 0.1)
    elseif (e.ends or 0) - GetTime() <= 180 then
        tooltip:AddLine(L.soon, 1, 0.5, 0.25)
    end
    rewardLines(tooltip, rewards[e.id])
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
        -- the map shown may not be the player's: keep the last drawing, without the
        -- Expeditions that ended since
        local kept = mini.kept
        if kept then
            for i = #kept, 1, -1 do
                if not expeditions[kept[i].e.id] then table.remove(kept, i) end
            end
        end
        return kept
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
-- TRACKER: the modern client's Mythic+ block
-- ---------------------------------------------------------------------------

-- Pieces of Interface\WowFates\Tracker (512 x 256): left, top, width, height
local SHEET_W, SHEET_H = 512, 256
local PIECE = {
    timer = { 0, 0, 261, 87 }, timerBack = { 0, 88, 223, 11 }, timerBG = { 0, 100, 223, 11 },
    timerFill = { 0, 112, 223, 11 }, barFrame = { 0, 124, 240, 51 },
}
local BLOCK_W, BLOCK_H = 251, 87            -- ScenarioObjectiveTrackerChallengeModeMixin
local BAR_W, BAR_H = 191, 17                -- ScenarioProgressBarTemplate's bar
local ORANGE = { 1, 0.55, 0.1 }
local BAR_BG = { 0.18, 0.07, 0.04 }         -- the bar's (0.04, 0.07, 0.18), orange side
local FONT = "Fonts\\FRIZQT__.TTF"
local GREEN, GREY, RED = { 0.1, 1, 0.1 }, { 0.6, 0.6, 0.6 }, { 1, 0.1, 0.1 }

local function piece(tex, name)
    local p = PIECE[name]
    tex:SetTexture(TRACKER)
    tex:SetTexCoord(p[1] / SHEET_W, (p[1] + p[3]) / SHEET_W, p[2] / SHEET_H, (p[2] + p[4]) / SHEET_H)
end

local function text(parent, size, layer, flags)
    local fs = parent:CreateFontString(nil, layer or "OVERLAY")
    fs:SetFont(FONT, size, flags)
    fs:SetShadowOffset(1, -1)
    fs:SetShadowColor(0, 0, 0, 1)
    fs:SetJustifyH("LEFT")
    return fs
end

-- What the block shows: the Expedition the player is in, or the one he left or saw end, for
-- LINGER seconds
track.id = nil
track.copy = nil                            -- the Expedition's last values (it may be gone)
track.kills, track.part, track.pct = 0, 0, 0
track.result = nil                          -- "success", "failed" or "left"
track.untilTime = nil
track.syncLevel, track.syncPct = 0, 100

local block

local function buildBlock()
    if block or not WatchFrameLines then return block end
    local b = CreateFrame("Frame", "WowFatesTrackerBlock", WatchFrameLines)
    b:SetWidth(BLOCK_W)
    b:SetHeight(BLOCK_H)
    b:Hide()

    -- the timer: its frame, its bar's back, track and fill, its texts
    local timer = CreateFrame("Frame", nil, b)
    timer:SetWidth(BLOCK_W)
    timer:SetHeight(BLOCK_H)
    timer:SetPoint("TOPLEFT", b, "TOPLEFT", 0, 0)
    local back = timer:CreateTexture(nil, "BACKGROUND")
    piece(back, "timerBack")
    back:SetWidth(223) back:SetHeight(11)
    back:SetPoint("BOTTOM", timer, "BOTTOM", 0, 13)
    local bg = timer:CreateTexture(nil, "BORDER")
    piece(bg, "timerBG")
    bg:SetWidth(223) bg:SetHeight(11)
    bg:SetPoint("BOTTOM", timer, "BOTTOM", 0, 13)
    local fill = timer:CreateTexture(nil, "ARTWORK")
    piece(fill, "timerFill")
    fill:SetHeight(11)
    fill:SetPoint("LEFT", bg, "LEFT", 0, 0)
    local frame = timer:CreateTexture(nil, "OVERLAY")
    piece(frame, "timer")
    frame:SetAllPoints(timer)
    local title = text(timer, 13, "OVERLAY")
    title:SetPoint("TOPLEFT", timer, "TOPLEFT", 28, -18)
    title:SetWidth(BLOCK_W - 50)
    title:SetTextColor(ORANGE[1], ORANGE[2], ORANGE[3])
    local timeLeft = text(timer, 22, "OVERLAY")
    timeLeft:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 0, -6)
    timeLeft:SetTextColor(1, 1, 1)

    -- the objective: its line, then its bar in percent
    local objective = text(b, 12)
    objective:SetPoint("TOPLEFT", timer, "BOTTOMLEFT", 12, -4)
    objective:SetWidth(BLOCK_W - 24)
    objective:SetTextColor(0.8, 0.8, 0.8)
    local bar = CreateFrame("StatusBar", nil, b)
    bar:SetWidth(BAR_W)
    bar:SetHeight(BAR_H)
    bar:SetPoint("TOPLEFT", objective, "BOTTOMLEFT", 10, -12)
    bar:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar")
    bar:SetStatusBarColor(ORANGE[1], ORANGE[2], ORANGE[3])
    bar:SetMinMaxValues(0, 100)
    local barBG = bar:CreateTexture(nil, "BACKGROUND")
    barBG:SetAllPoints(bar)
    barBG:SetTexture(BAR_BG[1], BAR_BG[2], BAR_BG[3])
    local barFrame = bar:CreateTexture(nil, "OVERLAY")
    piece(barFrame, "barFrame")
    barFrame:SetWidth(240) barFrame:SetHeight(51)
    barFrame:SetPoint("LEFT", bar, "LEFT", -8, -1)
    local barText = text(bar, 13, "OVERLAY")
    barText:SetPoint("CENTER", bar, "CENTER", -1, -1)
    barText:SetJustifyH("CENTER")
    barText:SetTextColor(1, 1, 1)

    -- the result, at the end, over the rewards (no participation shown)
    local part = text(b, 12)
    part:SetPoint("TOPLEFT", bar, "BOTTOMLEFT", -10, -14)
    part:SetWidth(BLOCK_W - 24)
    local kills = text(b, 11)
    kills:SetPoint("TOPLEFT", part, "BOTTOMLEFT", 0, -2)
    kills:SetTextColor(0.7, 0.7, 0.7)

    -- what the participation reached gives: amounts, then the items
    local rewHeader = text(b, 12)
    rewHeader:SetPoint("TOPLEFT", bar, "BOTTOMLEFT", -10, -14)
    rewHeader:SetTextColor(1, 0.82, 0)
    rewHeader:SetText(L.expected)
    local rewText = text(b, 12)
    rewText:SetPoint("TOPLEFT", rewHeader, "BOTTOMLEFT", 0, -3)
    rewText:SetWidth(BLOCK_W - 24)
    rewText:SetTextColor(1, 1, 1)

    b.timer, b.fill, b.title, b.timeLeft = timer, fill, title, timeLeft
    b.objective, b.bar, b.barText, b.part, b.kills = objective, bar, barText, part, kills
    b.rewHeader, b.rewText, b.items = rewHeader, rewText, {}
    b.baseHeight = BLOCK_H + 4 + 14 + 12 + BAR_H + 14 + 15 + 15
    b.height = b.baseHeight
    block = b
    return b
end

local ITEM_SIDE, ITEM_GAP = 24, 4

local function itemButton(b, i)
    local btn = b.items[i]
    if btn then return btn end
    btn = CreateFrame("Button", nil, b)
    btn:SetWidth(ITEM_SIDE)
    btn:SetHeight(ITEM_SIDE)
    btn.icon = btn:CreateTexture(nil, "ARTWORK")
    btn.icon:SetAllPoints(btn)
    btn.count = btn:CreateFontString(nil, "OVERLAY")
    btn.count:SetFont(FONT, 11, "OUTLINE")
    btn.count:SetPoint("BOTTOMRIGHT", btn, "BOTTOMRIGHT", -1, 1)
    btn.cross = btn:CreateTexture(nil, "OVERLAY")
    btn.cross:SetTexture("Interface\\Buttons\\UI-GroupLoot-Pass-Up")
    btn.cross:SetWidth(14)
    btn.cross:SetHeight(14)
    btn.cross:SetPoint("TOPRIGHT", btn, "TOPRIGHT", 2, 2)
    btn:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        if self.hidden then
            GameTooltip:SetText("???", 1, 0.13, 0.13)
        else
            GameTooltip:SetHyperlink("item:" .. self.entry .. ":0:0:0:0:0:0:0")
        end
        GameTooltip:Show()
    end)
    btn:SetScript("OnLeave", function() GameTooltip:Hide() end)
    b.items[i] = btn
    return btn
end

-- The rewards rows; returns the block's height
local function fillRewards(b)
    local tier = track.part or 0
    local got = shareOf(rewards[track.id], tier, track.pct or 0)
    local amounts = {}
    if got.money > 0 then amounts[#amounts + 1] = coins(got.money) end
    if got.xp > 0 then amounts[#amounts + 1] = string.format(L.xp, got.xp) end
    local anyItem = false
    for _, it in ipairs(got.items) do if it.granted then anyItem = true end end
    if #amounts == 0 and not anyItem then
        b.rewText:SetText(L.noReward)
        b.rewText:SetTextColor(0.6, 0.6, 0.6)
    else
        b.rewText:SetText(table.concat(amounts, "   "))
        b.rewText:SetTextColor(1, 1, 1)
    end
    for i, it in ipairs(got.items) do
        local btn = itemButton(b, i)
        btn.entry, btn.hidden, btn.granted = it.entry, it.hidden, it.granted
        if it.hidden then
            btn.icon:SetTexture(QUESTION)
            btn.icon:SetVertexColor(1, 0.25, 0.25)
            btn.count:SetText("")
        else
            local _, icon = itemInfo(it.entry)
            btn.icon:SetTexture(icon)
            btn.icon:SetVertexColor(1, 1, 1)
            btn.count:SetText(it.count > 1 and it.count or "")
        end
        if btn.icon.SetDesaturated then btn.icon:SetDesaturated(not it.granted) end
        btn:SetAlpha(it.granted and 1 or 0.6)
        if it.granted then btn.cross:Hide() else btn.cross:Show() end
        btn:ClearAllPoints()
        btn:SetPoint("TOPLEFT", b.rewText, "BOTTOMLEFT", (i - 1) * (ITEM_SIDE + ITEM_GAP), -4)
        btn:Show()
    end
    for i = #got.items + 1, #b.items do b.items[i]:Hide() end
    return b.baseHeight + (#got.items > 0 and (ITEM_SIDE + 6) or 0)
end

local function color(fs, c)
    fs:SetTextColor(c[1], c[2], c[3])
end

-- Fills the block from what is known now
local function fillBlock()
    local b, e = block, track.copy
    if not (b and e) then return end
    b.title:SetText(string.format(L.title, e.name or "", e.level or 0))
    -- the progress in percent (the module sends it out of 100): no count shown
    local count, progress = e.count or 0, math.min(e.progress or 0, e.count or 0)
    b.objective:SetText(e.label or e.objective or "")
    local pct = count > 0 and math.floor(progress * 100 / count) or 0
    b.bar:SetValue(pct)
    b.barText:SetText(pct .. "%")
    -- the objective, the progress and the rewards only: the participation is not shown;
    -- at the end, the result takes the rewards' header line
    b.kills:Hide()
    if track.result == "success" or track.result == "failed" then
        b.part:SetText(track.result == "success" and L.success or L.failed)
        color(b.part, track.result == "success" and GREEN or RED)
        b.part:Show()
        b.rewHeader:Hide()
    else
        b.part:Hide()
        b.rewHeader:Show()
    end
    -- a new height lays the tracker out again
    local height = fillRewards(b)
    if height ~= b.height then
        b.height = height
        return true
    end
end

-- The timer's place: the players do not see how long an Expedition waits for
-- them; it stays empty
local function tickBlock()
    local b, e = block, track.copy
    if not (b and e and b:IsShown()) then return end
    b.timeLeft:SetText("")
    b.fill:Hide()
end

local function shown()
    if track.id and not track.result then return true end
    return track.untilTime and GetTime() < track.untilTime
end

-- WatchFrame's handler (WatchFrame_Update): lineFrame, the offset reached (negative), the
-- frame's height and width; returns the height used, the width and the number of objectives,
-- always three numbers: 3.3.5 adds up the third (WatchFrame.lua, totalObjectives)
local function handler(lineFrame, offset, maxHeight, frameWidth)
    local b = buildBlock()
    if not b then return 0, 0, 0 end
    if not shown() then
        b:Hide()
        return 0, 0, 0
    end
    -- ForeverUI's tracker is 260 wide; the game's may be narrower: the block is scaled down
    local width = (ForeverUI and ForeverUI.ObjectiveTracker and ForeverUI.ObjectiveTracker.header) and 260
        or (frameWidth or BLOCK_W)
    local scale = math.min(1, width / (BLOCK_W + 6))
    b:SetScale(scale)
    b:ClearAllPoints()
    b:SetPoint("TOPLEFT", lineFrame, "TOPLEFT", math.max(0, (width - BLOCK_W * scale) / 2) / scale, offset / scale)
    fillBlock()
    b:Show()
    tickBlock()
    return b.height * scale, BLOCK_W * scale, 1
end

local function relayout()
    if WatchFrame_Update and WatchFrame then WatchFrame_Update(WatchFrame) end
end

local function install()
    if track.installed or not WATCHFRAME_OBJECTIVEHANDLERS then return end
    -- first: the block heads the tracker, above the quests
    table.insert(WATCHFRAME_OBJECTIVEHANDLERS, 1, handler)
    track.installed = true
end

-- ---------------------------------------------------------------------------
-- BANNER: the middle of the screen, as the modern client's scenario banners
-- ---------------------------------------------------------------------------

local banner
local FADE_IN, HOLD, FADE_OUT = 0.5, 3, 1

local function buildBanner()
    if banner then return banner end
    local f = CreateFrame("Frame", "WowFatesBanner", UIParent)
    f:SetWidth(600)
    f:SetHeight(80)
    f:SetPoint("TOP", UIParent, "TOP", 0, -180)
    f:SetFrameStrata("HIGH")
    f:Hide()
    local title = f:CreateFontString(nil, "OVERLAY")
    title:SetFont("Fonts\\MORPHEUS.TTF", 30, "OUTLINE")
    title:SetShadowOffset(2, -2)
    title:SetShadowColor(0, 0, 0, 1)
    title:SetPoint("TOP", f, "TOP", 0, 0)
    title:SetTextColor(ORANGE[1], ORANGE[2], ORANGE[3])
    local sub = f:CreateFontString(nil, "OVERLAY")
    sub:SetFont(FONT, 17, "OUTLINE")
    sub:SetPoint("TOP", title, "BOTTOM", 0, -6)
    sub:SetTextColor(1, 0.82, 0)
    f.title, f.sub, f.t = title, sub, 0
    f:SetScript("OnUpdate", function(self, elapsed)
        self.t = self.t + elapsed
        local t = self.t
        if t < FADE_IN then
            self:SetAlpha(t / FADE_IN)
        elseif t < FADE_IN + HOLD then
            self:SetAlpha(1)
        elseif t < FADE_IN + HOLD + FADE_OUT then
            self:SetAlpha(1 - (t - FADE_IN - HOLD) / FADE_OUT)
        else
            self:Hide()
        end
    end)
    banner = f
    return f
end

local function play(sound)
    if PlaySoundFile then pcall(PlaySoundFile, sound) end
end

local function showBanner(title, sub, sound)
    local f = buildBanner()
    f.title:SetText(title or "")
    f.sub:SetText(sub or "")
    f.t = 0
    f:SetAlpha(0)
    f:Show()
    if sound then play(sound) end
end

-- ---------------------------------------------------------------------------
-- THE MESSAGES' EFFECTS
-- ---------------------------------------------------------------------------

local function copy(e)
    local c = {}
    for k, v in pairs(e) do c[k] = v end
    return c
end

function track.update(e)
    if track.id == e.id then
        track.copy = copy(e)
        if block and block:IsShown() and fillBlock() then relayout() end
    end
end

function track.enter(id)
    local e = expeditions[id]
    if track.id ~= id then track.kills, track.part, track.pct = 0, 0, 0 end
    track.id, track.result, track.untilTime = id, nil, nil
    if e then track.copy = copy(e) end
    local c = track.copy
    if c then
        showBanner(string.format(L.banner, c.name or ""), string.format(L.bannerLevel, c.level or 0),
            "Sound\\Interface\\iQuestActivate.wav")
    end
    relayout()
end

function track.leave(id)
    if track.id ~= id then return end
    track.result, track.untilTime = "left", GetTime() + LINGER
    if track.copy then track.copy.frozen = math.max(0, (track.copy.ends or 0) - GetTime()) end
    showBanner(L.leaving, track.copy and track.copy.name or "", "Sound\\Interface\\iQuestLogCloseA.wav")
    relayout()
end

function track.finish(id, success)
    if track.id ~= id then return end
    track.result, track.untilTime = success and "success" or "failed", GetTime() + LINGER
    if track.copy then
        track.copy.frozen = math.max(0, (track.copy.ends or 0) - GetTime())
        if success then track.copy.progress = track.copy.count end
    end
    if not success then
        showBanner(L.failed, track.copy and track.copy.name or "", "Sound\\Interface\\iQuestLogCloseA.wav")
    end
    relayout()
end

-- A boss out, a defence begun: a banner with ALERT_SOUND; a wave coming: WAVE_SOUND
function track.alert(id, title, sound)
    if track.id ~= id or track.result then return end
    showBanner(title, track.copy and track.copy.name or "", sound or ALERT_SOUND)
end

function track.status(id, kills, part, pct)
    if track.id ~= id then return end
    track.kills, track.part, track.pct = kills, part, pct
    if block and block:IsShown() and fillBlock() then relayout() end
end

function track.sync(level, pct)
    track.syncLevel, track.syncPct = level, pct
end

-- The block's own clock: the timer each frame, the tracker laid out again when it expires
local trackerClock = CreateFrame("Frame")
trackerClock:SetScript("OnUpdate", function()
    if not track.installed then install() end
    tickBlock()
    if track.untilTime and GetTime() >= track.untilTime then
        track.untilTime = nil
        if track.result then track.id, track.result, track.copy = nil, nil, nil end
        relayout()
    end
end)


-- ---------------------------------------------------------------------------
-- WHAT WAS WON: a window at the top of the screen, as the modern client's
-- "challenge complete" banner; it fades in, stays 5 seconds, fades out
-- ---------------------------------------------------------------------------

local WON_HOLD, WON_FADE = 5, 0.5
local WON_ICON, WON_GAP = 36, 8
local wonFrame

local function buildWon()
    if wonFrame then return wonFrame end
    local f = CreateFrame("Frame", "WowFatesWonFrame", UIParent)
    f:SetWidth(380)
    f:SetHeight(150)
    f:SetPoint("TOP", UIParent, "TOP", 0, -130)
    f:SetFrameStrata("HIGH")
    f:Hide()
    local bg = f:CreateTexture(nil, "BACKGROUND")
    bg:SetAllPoints(f)
    bg:SetTexture(0, 0, 0, 0.55)
    local top = f:CreateTexture(nil, "BORDER")
    top:SetTexture(1, 0.55, 0.1, 0.9)
    top:SetHeight(2)
    top:SetPoint("TOPLEFT", f, "TOPLEFT", 0, 0)
    top:SetPoint("TOPRIGHT", f, "TOPRIGHT", 0, 0)
    local bottom = f:CreateTexture(nil, "BORDER")
    bottom:SetTexture(1, 0.55, 0.1, 0.9)
    bottom:SetHeight(2)
    bottom:SetPoint("BOTTOMLEFT", f, "BOTTOMLEFT", 0, 0)
    bottom:SetPoint("BOTTOMRIGHT", f, "BOTTOMRIGHT", 0, 0)
    local title = f:CreateFontString(nil, "OVERLAY")
    title:SetFont("Fonts\\MORPHEUS.TTF", 30, "OUTLINE")
    title:SetPoint("TOP", f, "TOP", 0, -10)
    title:SetTextColor(1, 0.55, 0.1)
    local name = f:CreateFontString(nil, "OVERLAY")
    name:SetFont(FONT, 15, "OUTLINE")
    name:SetPoint("TOP", title, "BOTTOM", 0, -4)
    name:SetTextColor(1, 0.82, 0)
    local earned = f:CreateFontString(nil, "OVERLAY")
    earned:SetFont(FONT, 13)
    earned:SetPoint("TOP", name, "BOTTOM", 0, -8)
    earned:SetTextColor(1, 1, 1)
    local amounts = f:CreateFontString(nil, "OVERLAY")
    amounts:SetFont(FONT, 13)
    amounts:SetTextColor(1, 1, 1)
    f.title, f.name, f.earned, f.amounts, f.icons, f.t = title, name, earned, amounts, {}, 0
    f:SetScript("OnUpdate", function(self, elapsed)
        self.t = self.t + elapsed
        local t = self.t
        if t < WON_FADE then
            self:SetAlpha(t / WON_FADE)
        elseif t < WON_FADE + WON_HOLD then
            self:SetAlpha(1)
        elseif t < 2 * WON_FADE + WON_HOLD then
            self:SetAlpha(1 - (t - WON_FADE - WON_HOLD) / WON_FADE)
        else
            self:Hide()
        end
    end)
    wonFrame = f
    return f
end

local function wonIcon(f, i)
    local b = f.icons[i]
    if b then return b end
    b = CreateFrame("Button", nil, f)
    b:SetWidth(WON_ICON)
    b:SetHeight(WON_ICON)
    b.icon = b:CreateTexture(nil, "ARTWORK")
    b.icon:SetAllPoints(b)
    b.count = b:CreateFontString(nil, "OVERLAY")
    b.count:SetFont(FONT, 12, "OUTLINE")
    b.count:SetPoint("BOTTOMRIGHT", b, "BOTTOMRIGHT", -2, 2)
    b:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetHyperlink("item:" .. self.entry .. ":0:0:0:0:0:0:0")
        GameTooltip:Show()
    end)
    b:SetScript("OnLeave", function() GameTooltip:Hide() end)
    f.icons[i] = b
    return b
end

function track.won(id, money, xp, items)
    local f = buildWon()
    if banner then banner:Hide() end           -- the window takes the banner's place
    local e = expeditions[id] or (track.copy and track.copy.id == id and track.copy)
    f.name:SetText(e and e.name or "")
    local amounts = {}
    if money > 0 then amounts[#amounts + 1] = coins(money) end
    if xp > 0 then amounts[#amounts + 1] = string.format(L.xp, xp) end
    local nothing = #amounts == 0 and #items == 0
    f.title:SetText(nothing and L.done or L.bravo)
    f.earned:SetText(nothing and "" or L.earned)
    f.amounts:SetText(table.concat(amounts, "   "))

    local height = 75
    local rowWidth = #items * WON_ICON + math.max(#items - 1, 0) * WON_GAP
    for i, it in ipairs(items) do
        local b = wonIcon(f, i)
        local _, icon = itemInfo(it.entry)
        b.entry = it.entry
        b.icon:SetTexture(icon)
        b.count:SetText(it.count > 1 and it.count or "")
        b:ClearAllPoints()
        b:SetPoint("TOPLEFT", f, "TOP", -rowWidth / 2 + (i - 1) * (WON_ICON + WON_GAP), -height - 22)
        b:Show()
    end
    for i = #items + 1, #f.icons do f.icons[i]:Hide() end
    if not nothing then height = height + 22 end
    if #items > 0 then height = height + WON_ICON + 8 end
    f.amounts:ClearAllPoints()
    f.amounts:SetPoint("TOP", f, "TOP", 0, -height - 4)
    if #amounts > 0 then height = height + 20 end
    f:SetHeight(height + 12)

    f.t = 0
    f:SetAlpha(0)
    f:Show()
    play(nothing and "Sound\\Interface\\iQuestLogCloseA.wav" or "Sound\\Interface\\iQuestComplete.wav")
end

-- ---------------------------------------------------------------------------
-- THE SYNCED DEBUFF'S TOOLTIP
-- ---------------------------------------------------------------------------

local function auraSpell(unit, index, filter, getter)
    if not (unit and index) then return nil end
    local spellId = select(11, getter(unit, index, filter))
    return spellId
end

local function addSyncedLine(tooltip, spellId)
    if spellId ~= SYNCED_SPELL or (track.syncLevel or 0) <= 0 then return end
    tooltip:AddLine(string.format(L.synced, track.syncLevel, track.syncPct or 100), 1, 0.82, 0, true)
    tooltip:Show()
end

if GameTooltip and hooksecurefunc then
    if GameTooltip.SetUnitAura then
        hooksecurefunc(GameTooltip, "SetUnitAura", function(self, unit, index, filter)
            addSyncedLine(self, auraSpell(unit, index, filter, UnitAura))
        end)
    end
    if GameTooltip.SetUnitDebuff then
        hooksecurefunc(GameTooltip, "SetUnitDebuff", function(self, unit, index, filter)
            addSyncedLine(self, auraSpell(unit, index, filter, UnitDebuff))
        end)
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
