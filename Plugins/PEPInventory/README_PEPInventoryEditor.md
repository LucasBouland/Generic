# PEP Items Editor Browser

`PEPInventoryEditor` fournit un outil éditeur pour explorer les définitions d’items et les acteurs PEP présents dans le niveau ouvert. L’outil ne fait pas partie du jeu empaqueté.

## Accès

- Menu : **Tools > PEP > Items**.
- Asset EUW : `Plugins/PEPInventory/Content/Editor/UI/EUW_PEPItemBrowser.uasset`.
- Classe native parente : `UPEPItemBrowserEditorWidget`, dans le module Editor `PEPInventoryEditor`.

Le widget utilise actuellement une présentation Slate native, hébergée par un Editor Utility Widget Blueprint. Cela permet de réutiliser le navigateur Slate des définitions et ses panneaux de détails/aperçu existants.

## Vue Item Definitions

- Liste les assets `UPEPItemDefinition` détectés par l’Asset Registry.
- Le panneau latéral liste les catégories racines dérivées de `Item.Type.*`. Par exemple, `Item.Type.Weapon.Rifle` est affiché dans la liste sous `Item.Type.Weapon`. ItemCore ne déclare que la racine `Item.Type` (`PEPItemTags::Item_Type`) : les catégories elles-mêmes sont les tags que le projet déclare sous cette racine.
- La table montre le nom affiché, le chemin `Item.Type` complet, le nom d’asset et les fragments.
- Sélectionner une ligne puis utiliser **Open Details Window** ouvre les propriétés éditables dans une fenêtre Slate séparée. Un double-clic sur une ligne ouvre la même fenêtre.
- Cette fenêtre suit la sélection dans le navigateur tant qu’elle reste ouverte. Les modifications sont appliquées directement à l’asset via le panneau de détails; utiliser les commandes de sauvegarde habituelles de l’éditeur pour les enregistrer.
- L’aperçu des slots de conteneur reste visible sous les propriétés. **Open Asset Editor** ouvre aussi l’éditeur standard de l’asset si ses autres outils sont nécessaires.
- **Refresh** relit l’Asset Registry et reconstruit la liste.

## Vue Items in Level

- Liste les `APEPItemWorldActor` et leurs sous-classes retournés par `UEditorActorSubsystem::GetAllLevelActors`.
- Actualisation manuelle; aucune recherche du monde à chaque Tick.
- Affiche le label, la classe, la définition runtime ou la source configurée (définition directe/preset) et la position.
- Affiche les propriétés de l’acteur sélectionné dans un panneau de détails en lecture seule.
- **Select in Level** sélectionne l’acteur; **Focus Viewport** le cadre. Un double-clic fait les deux.
- La liste est en lecture seule pour les opérations métier d’items : elle ne crée, ne déplace et ne détruit aucun item.

L’instance et l’arbre runtime ne sont pas toujours disponibles sur un acteur placé dans l’éditeur. La vue affiche donc la recette de spawn lorsque l’acteur n’a pas encore d’instance exploitable, plutôt que de déduire ou inventer son état.

## Limites V1

- Pas de filtre de texte ou de classe pour les acteurs du niveau.
- Pas encore d’affichage de l’ID runtime d’item ni du contenu récursif d’un conteneur.
- L’actualisation de la liste des acteurs est manuelle.
- Le panneau de détails de la vue de niveau reste strictement non éditable.

Les définitions sont des données partagées; la vue de niveau décrit les acteurs PEP placés dans le monde actuellement ouvert et les informations disponibles sur leur source/runtime.
