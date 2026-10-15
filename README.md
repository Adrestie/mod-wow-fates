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
