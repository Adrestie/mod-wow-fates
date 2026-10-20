#!/usr/bin/env python3
# mod-wow-fates — writes the module's spells on both sides from one description:
#   data/sql/world/wow_fates_03_spells.sql   the server's rows (spell_dbc)
#   installer.json, entry "Spell.dbc"        the client's rows
#
# Run from the module's folder: python tools/gen_spells.py

import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Attributes
ATTR0_DO_NOT_DISPLAY = 0x00000080
ATTR0_NO_IMMUNITIES = 0x20000000
ATTR0_NO_AURA_CANCEL = 0x80000000
ATTR1_NO_AURA_ICON = 0x10000000
ATTR2_ALLOW_DEAD_TARGET = 0x00000001
ATTR3_ALLOW_AURA_WHILE_DEAD = 0x00100000

ATTR1_CHANNELED = 0x00000004

# Auras
DUMMY = 4
MOD_DAMAGE_PERCENT_DONE = 79
MOD_RESISTANCE_PCT = 101
MOD_INCREASE_ENERGY_PERCENT = 132
MOD_INCREASE_HEALTH_PERCENT = 133
MOD_HEALING_DONE_PERCENT = 136
MOD_TOTAL_STAT_PERCENTAGE = 137
MOD_RATING = 189


def cr(*ratings):
    return sum(1 << r for r in ratings)


CR_DEFENSE, CR_DODGE, CR_PARRY = 1, 2, 3
CR_HIT = (5, 6, 7)
CR_CRIT = (8, 9, 10)
CR_RESILIENCE = (14, 15, 16)
CR_HASTE = (17, 18, 19)
CR_EXPERTISE, CR_ARMOR_PENETRATION = 23, 24

LANGS = ("enUS", "frFR", "deDE", "esES", "ruRU")

SYNCED_NAME = {
    "enUS": "Synced",
    "frFR": "Synchronisé",
    "deDE": "Synchronisiert",
    "esES": "Sincronizado",
    "ruRU": "Синхронизация",
}

SYNCED_TEXT = {
    "enUS": "Synced to the level of the Expedition, shown on the icon: your stats, ratings, health, armor, damage and healing are reduced to match it.",
    "frFR": "Synchronisé au niveau de l'Expédition, indiqué sur l'icône : vos caractéristiques, scores, points de vie, armure, dégâts et soins sont réduits en conséquence.",
    "deDE": "Auf die Stufe der Expedition synchronisiert, die das Symbol anzeigt: Eure Werte, Wertungen, Gesundheit, Rüstung, Euer Schaden und Eure Heilung sind entsprechend verringert.",
    "esES": "Sincronizado al nivel de la Expedición, indicado en el icono: tus atributos, índices, salud, armadura, daño y sanación se reducen en consecuencia.",
    "ruRU": "Синхронизация с уровнем экспедиции, указанным на значке: ваши характеристики, рейтинги, здоровье, броня, наносимый урон и исцеление снижены.",
}

RITUAL_NAME = {
    "enUS": "Ritual",
    "frFR": "Rituel",
    "deDE": "Ritual",
    "esES": "Ritual",
    "ruRU": "Ритуал",
}

RITUAL_TEXT = {
    "enUS": "Channels the ritual of the Expedition into the altar.",
    "frFR": "Canalise le rituel de l'Expédition dans l'autel.",
    "deDE": "Kanalisiert das Ritual der Expedition in den Altar.",
    "esES": "Canaliza el ritual de la Expedición en el altar.",
    "ruRU": "Направляет ритуал экспедиции в алтарь.",
}

VISIBLE = ATTR0_NO_AURA_CANCEL | ATTR0_NO_IMMUNITIES
HIDDEN = VISIBLE | ATTR0_DO_NOT_DISPLAY

# id, name (all languages or English), texts, attributes, attributesEx, icon,
# effects as (aura, misc)
SPELLS = [
    (96000, SYNCED_NAME, SYNCED_TEXT, VISIBLE, 0, 58,
     [(MOD_TOTAL_STAT_PERCENTAGE, -1), (MOD_DAMAGE_PERCENT_DONE, 127), (MOD_HEALING_DONE_PERCENT, 0)]),
    (96001, {"enUS": "Synced (offense)"}, None, HIDDEN, ATTR1_NO_AURA_ICON, 58,
     [(MOD_RATING, cr(*CR_HIT)), (MOD_RATING, cr(*CR_CRIT)), (MOD_RATING, cr(*CR_HASTE))]),
    (96002, {"enUS": "Synced (defense)"}, None, HIDDEN, ATTR1_NO_AURA_ICON, 58,
     [(MOD_RATING, cr(CR_DEFENSE)), (MOD_RATING, cr(CR_DODGE, CR_PARRY)), (MOD_RATING, cr(CR_EXPERTISE, CR_ARMOR_PENETRATION))]),
    (96003, {"enUS": "Synced (health)"}, None, HIDDEN, ATTR1_NO_AURA_ICON, 58,
     [(MOD_INCREASE_HEALTH_PERCENT, 0), (MOD_RESISTANCE_PCT, 1), (MOD_INCREASE_ENERGY_PERCENT, 0)]),
    # The invocation's channel, cast by the player at the altar's focus (an
    # invisible creature on it): a channel of ten seconds with the bar that
    # empties, the hands raised (the look of the game's Blue Beam, visual 6500)
    # and a beam to the altar; a blow, moving or a cast breaks it. The module
    # casts it again while the player stays.
    (96004, RITUAL_NAME, RITUAL_TEXT, 0, ATTR1_CHANNELED, 1480,
     [(DUMMY, 0)]),
]

# What a spell has of its own, over the rows above: DBC field -> value.
EXTRA = {
    96004: {
        6: 0, 7: 0,                       # no Synced attributes
        10: 0x8,                          # AttributesEx6: may aim at the focus, immune to players
        31: 0x0F,                         # InterruptFlags
        33: 0x100F,                       # ChannelInterruptFlags: hit, damage, cast, move
        40: 1,                            # DurationIndex: 10 s
        46: 7,                            # RangeIndex: 10 yards
        80: 0,                            # EffectBasePoints_1
        86: 25,                           # ImplicitTargetA_1: the unit aimed at
        131: 6500,                        # SpellVisualID_1: Blue Beam
        225: 64,                          # SchoolMask: arcane
    },
}

# spell_dbc's names of those fields.
EXTRA_COLUMNS = {
    6: "AttributesEx2", 7: "AttributesEx3", 10: "AttributesEx6", 31: "InterruptFlags", 33: "ChannelInterruptFlags",
    40: "DurationIndex", 46: "RangeIndex", 80: "EffectBasePoints_1", 86: "ImplicitTargetA_1",
    131: "SpellVisualID_1", 225: "SchoolMask",
}

LANG_MASK = 16712190
FLOAT_ONE = 1065353216

# Spell.dbc: offset of each language in a localized string.
DBC_LANG = {"enUS": 0, "frFR": 2, "deDE": 3, "esES": 6, "esMX": 7, "ruRU": 8}
# spell_dbc: the columns follow the DBC by position, whatever their names.
SQL_LANG = {"enUS": "enUS", "frFR": "koKR", "deDE": "frFR", "esES": "zhCN", "esMX": "enTW", "ruRU": "zhTW"}


def localized(texts):
    out = dict(texts)
    if "esES" in out:
        out.setdefault("esMX", out["esES"])
    return out


def sql_value(v):
    if isinstance(v, str):
        return "'" + v.replace("\\", "\\\\").replace("'", "''") + "'"
    if isinstance(v, float):
        return repr(v)
    return str(v)


def sql_rows():
    lines = []
    for sid, names, texts, attr, attr1, icon, effects in SPELLS:
        cols = {
            "ID": sid,
            "Attributes": attr,
            "AttributesEx": attr1,
            "AttributesEx2": ATTR2_ALLOW_DEAD_TARGET,
            "AttributesEx3": ATTR3_ALLOW_AURA_WHILE_DEAD,
            "CastingTimeIndex": 1,
            "ProcChance": 101,
            "DurationIndex": 21,
            "RangeIndex": 1,
            "EquippedItemClass": -1,
            "SpellIconID": icon,
            "SchoolMask": 1,
        }
        for i, (aura, misc) in enumerate(effects, 1):
            cols[f"Effect_{i}"] = 6
            cols[f"EffectDieSides_{i}"] = 1
            cols[f"EffectBasePoints_{i}"] = -2
            cols[f"ImplicitTargetA_{i}"] = 1
            cols[f"EffectAura_{i}"] = aura
            cols[f"EffectMiscValue_{i}"] = misc
            cols[f"EffectChainAmplitude_{i}"] = 1.0
        for lang, text in localized(names).items():
            cols[f"Name_Lang_{SQL_LANG[lang]}"] = text
        if texts:
            for lang, text in localized(texts).items():
                cols[f"Description_Lang_{SQL_LANG[lang]}"] = text
                cols[f"AuraDescription_Lang_{SQL_LANG[lang]}"] = text
        for field in ("Name", "NameSubtext", "Description", "AuraDescription"):
            cols[f"{field}_Lang_Mask"] = LANG_MASK
        for index, value in EXTRA.get(sid, {}).items():
            cols[EXTRA_COLUMNS[index]] = value

        names_sql = ", ".join(f"`{c}`" for c in cols)
        values_sql = ", ".join(sql_value(v) for v in cols.values())
        lines.append(f"INSERT INTO `spell_dbc` ({names_sql})\nVALUES ({values_sql});")
    return lines


def dbc_rows():
    rows = []
    for sid, names, texts, attr, attr1, icon, effects in SPELLS:
        row = {0: sid, 4: attr, 5: attr1, 6: ATTR2_ALLOW_DEAD_TARGET, 7: ATTR3_ALLOW_AURA_WHILE_DEAD,
               28: 1, 35: 101, 40: 21, 46: 1, 68: -1, 133: icon, 225: 1}
        for i, (aura, misc) in enumerate(effects):
            row[71 + i] = 6
            row[74 + i] = 1
            row[80 + i] = -2
            row[86 + i] = 1
            row[95 + i] = aura
            row[110 + i] = misc
            row[216 + i] = FLOAT_ONE
        for lang, text in localized(names).items():
            row[136 + DBC_LANG[lang]] = text
        if texts:
            for lang, text in localized(texts).items():
                row[170 + DBC_LANG[lang]] = text
                row[187 + DBC_LANG[lang]] = text
        for flags in (152, 169, 186, 203):
            row[flags] = LANG_MASK
        row.update(EXTRA.get(sid, {}))
        rows.append({str(k): row[k] for k in sorted(row)})
    return rows


def main():
    ids = ", ".join(str(s[0]) for s in SPELLS)
    header = f"""-- mod-wow-fates — the module's spells ({ids}, tranche 96 of the
-- WoW-mods registry). Generated by tools/gen_spells.py: edit the script, not
-- this file.
--
-- Server side: AzerothCore completes Spell.dbc from `spell_dbc` at startup;
-- the client has the same rows (installer.json, DBC Spell.dbc).
--
--   96000  Synced, the aura the player sees: all stats %, damage done %,
--          healing done %. Its charges show the level he is synced to.
--   96001  hidden: hit, critical strike and haste ratings
--   96002  hidden: defense rating, dodge and parry ratings, expertise and
--          armor penetration ratings
--   96003  hidden: maximum health %, armor %, maximum mana %
--   96004  Ritual: the channel of an invocation, at the altar's focus (ten
--          seconds, a beam, broken by a blow, moving or a cast)
--
-- Every amount of the Synced auras is set by the module (WowFatesSync::Apply); the -1 of the
-- base points only makes the auras harmful ones. They cannot be cancelled,
-- pierce invulnerabilities and stay through death.
--
-- Idempotent: every row is deleted before being written again.

DELETE FROM `spell_dbc` WHERE `ID` IN ({ids});
"""
    footer = f"""
-- Never saved with a character or a pet (SPELL_ATTR0_CU_AURA_CANNOT_BE_SAVED):
-- the area puts the debuff back while the Expedition lasts.
DELETE FROM `spell_custom_attr` WHERE `spell_id` IN ({ids});
INSERT INTO `spell_custom_attr` (`spell_id`, `attributes`) VALUES
""" + ",\n".join(f"({s[0]}, 16777216)" for s in SPELLS) + ";\n"
    path = os.path.join(ROOT, "data", "sql", "world", "wow_fates_03_spells.sql")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(header + "\n".join(sql_rows()) + "\n" + footer)

    path = os.path.join(ROOT, "installer.json")
    with open(path, encoding="utf-8") as f:
        manifest = json.load(f)
    for entry in manifest["dbc"]:
        if entry["file"] == "Spell.dbc":
            entry["rows"] = dbc_rows()
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)
        f.write("\n")


if __name__ == "__main__":
    main()
