# mod-wow-fates
AzerothCore mod for dynamic map events

# L'idée
des évenements scriptés dit "Expeditions" apparaissent à des points spécifiques de la carte.
différents objectifs (tuer des monstres, ramasser des objets, accompagner un PNJ qui doit survivre à des attaques de monstres, etc.)
des récompenses en fonction du niveau du personnage
pour éviter que les personnages de haut niveau farment facilement des Expéditions pour avoir des récompensent de haut niveau simplement, il faut mettre un scaling négatif temporaire si un joueur dépasse le niveau de l'Expedition. Ex : un niveau 80 arrive sur une Expédition de niveau 10 -> réduction de X% des statistiques

Les Expéditions doivent être affichées sur la carte
Un joueur doit participer activement à une Expedition pour prétendre à la récompense (se tenir sur le lieux ne suffit pas)

# Synchronisation de niveau : le debuff « Synchronisé » (« Synced »)

Lorsqu'un joueur trop puissant participe à une Expédition, il reçoit le debuff **Synchronisé**, qui ramène sa puissance au niveau prévu pour l'Expédition.

## Score de puissance

La puissance d'un personnage est mesurée par un **score de puissance** : la somme de ses statistiques principales et de ses scores de statistiques secondaires, lus en direct sur le personnage.

Le serveur comporte beaucoup de sources de puissance qui viennent d'auras, donc l'équipement pèsera de moins en moins dans la puissance d'un personnage. Lire les statistiques réelles prend en compte automatiquement l'équipement, les auras, les buffs et les bonus des autres mods.

## Calcul

- **Score du joueur** : mesuré quand le joueur entre dans l'Expédition, sans tenir compte du debuff Synchronisé lui-même.
- **Score de référence** : le score attendu d'un personnage au niveau de l'Expédition, calculé à partir des statistiques de base par niveau et de la courbe de gain de puissance des objets héritages (`ScalingStatValues.dbc`). Cette courbe est extrapolée jusqu'au niveau d'objet 300.
- **Ratio** = score de référence / score du joueur. Si le ratio est inférieur à 1, Synchronisé réduit les statistiques du personnage de ce ratio.

## Points à régler

1. **Pondération** : un point d'Endurance ne vaut pas un point de score de critique. Il faut pondérer les statistiques, par exemple en comptant les secondaires à 0,5, pour ne pas favoriser les classes à forte Endurance.
2. **Puissance d'attaque et puissance des sorts** : elles ne sont ni des statistiques principales ni des scores secondaires. Il faut les inclure si les auras du serveur en donnent beaucoup.
3. **Auras en pourcentage** : un bonus de type « +10 % de dégâts » ne change aucune statistique, donc le score ne le voit pas. Synchronisé doit aussi réduire les dégâts et les soins infligés, pas seulement les statistiques.
4. **Recalcul** : le score doit être recalculé quand le joueur gagne un buff pendant l'Expédition, à chaque nouvelle aura ou à intervalle régulier.

## Affichage

Le debuff est appliqué à l'entrée dans la zone de l'Expédition et retiré à la sortie ou à la fin de l'Expédition. Son infobulle indique le niveau de synchronisation (« Synchronisé niveau X »).

# État : jalon 1

> **Jalon 1, pas encore testé en jeu.** Le debuff Synchronisé, le gestionnaire
> d'Expéditions (apparition, durée, rotation), l'objectif « tuer N monstres »,
> la participation active et les récompenses par niveau sont écrits. L'affichage
> sur la carte et les objectifs collecte et escorte viendront ensuite.

Le module prend la tranche **96** (96000-96999) du
[registre WoW-mods](https://github.com/Adrestie/WoW-mods/blob/main/ID_RANGES.md) :
les sorts 96000-96003 sont le debuff Synchronisé. Un patch client pour une seule
chose : l'aura 96000 (nom, icône, infobulle) dans `Spell.dbc`, posée par
l'installeur WoW-mods.

## Synchronisé

| Sort | Visible | Effets |
|---|---|---|
| 96000 | oui, ses charges indiquent le niveau | toutes les caractéristiques %, dégâts infligés %, soins prodigués % |
| 96001 | non | scores de toucher, de critique, de hâte |
| 96002 | non | score de défense, d'esquive et de parade, d'expertise et de pénétration d'armure |
| 96003 | non | points de vie maximum %, armure %, résilience |

Toutes les valeurs sont posées par le module. Pour mesurer le joueur sans le
debuff, ses valeurs sont remises à 0, les statistiques lues, puis les nouvelles
valeurs posées, dans le même tick : le client ne voit que le résultat.

- **Score de puissance** : caractéristiques principales × 1 + scores
  secondaires × 0,5 (toucher, critique et hâte comptés une fois pour mêlée,
  distance et sorts) + puissance d'attaque venue de l'équipement et des auras
  (pas celle des caractéristiques, déjà comptées) ou puissance des sorts × 0,5.
- **Score de référence** : caractéristiques de base de la race et de la classe
  au niveau de l'Expédition + un budget d'équipement qui suit les héritages
  (`ScalingStatValues.dbc`, prolongé en ligne droite après le niveau 80), × un
  facteur de correction.
- **Ratio** = référence / score, au moins 0,05. En dessous de 1, chaque
  catégorie perd l'écart (réglable par catégorie). Points de vie et armure sont
  ramenés à la part du ratio après la baisse des caractéristiques.
- Mesure à l'entrée dans la zone puis toutes les 5 s ; retiré à la sortie, à la
  fin de l'Expédition, à la déconnexion.

## Expéditions

Tables du monde (`data/sql/world/wow_fates_04_schema.sql`) :

| Table | Contenu |
|---|---|
| `mod_wow_fates_expedition` | niveau, objectif (type 1 : tuer `objective_count` monstres, `objective_entry` ou n'importe lesquels), monstres amenés (`summon_*`), rayon, durée, temps de recharge, poids dans la rotation |
| `mod_wow_fates_expedition_locale` | nom dans les autres langues |
| `mod_wow_fates_spawn` | points d'apparition (continents seulement), un tiré au hasard |
| `mod_wow_fates_reward` | récompenses par niveau du joueur : argent, expérience, objet (par courrier si les sacs sont pleins) ; les lignes de l'Expédition, sinon celles de l'Expédition 0 |

Trois exemples (`wow_fates_05_examples.sql`, identifiants 1 à 99 réservés au
module) : Soulèvement kobold (forêt d'Elwynn, niveau 7), Raid tranchecrin
(Durotar, niveau 8), Morts sans repos (Couronne de glace, niveau 80).

**Participation active** : un joueur est récompensé s'il est près de
l'Expédition à la fin et a fait au moins un kill, ou des dégâts dans la zone et
des soins sur des joueurs en combat dans la zone valant au moins 25 % de la
contribution moyenne. Rester sur place ne rapporte rien.

## Commandes (MJ)

| Commande | Effet |
|---|---|
| `.fates list` | Expéditions actives |
| `.fates defs` | Expéditions chargées |
| `.fates start $id [$point]` | lance une Expédition |
| `.fates stop $id` | l'arrête, sans récompense |
| `.fates complete $id` | l'accomplit, récompenses données |
| `.fates reload` | recharge les tables (arrête les Expéditions actives) |
| `.fates score [$niveau]` | score du joueur ciblé, référence, ratio |

Réglages : `conf/mod-wow-fates.conf.dist`.
