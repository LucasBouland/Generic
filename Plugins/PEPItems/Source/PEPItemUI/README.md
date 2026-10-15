# PEPItemUI

`PEPItemUI` fournit une représentation visuelle réutilisable d’un item. Il dépend de `PEPItemCore` et `PEPCoreUI`, mais pas d’Inventory ni d’Equipment : afficher un item ne donne pas le droit de le déplacer ni de l’équiper.

## Widget de représentation

L’asset `WBP_PEPItemView` se trouve dans le contenu du plugin PEPItems :

- chemin Unreal : `/PEPItems/UI/WBP_PEPItemView`
- classe parente : `UPEPItemViewWidget`, basée sur CommonUI (`UCommonUserWidget`)
- hiérarchie initiale : `Border` → `HorizontalBox` → `Image` et colonne de textes

Le widget contient les widgets nommés et variables suivants, reliés aux `BindWidgetOptional` de la classe native :

| Nom | Type | Affichage |
| --- | --- | --- |
| `ItemIcon` | `Image` | Icône définie par l’item; chargée de façon asynchrone. |
| `ItemNameText` | `TextBlock` | Nom d’affichage de la définition. |
| `ItemStackText` | `TextBlock` | Quantité et taille maximale lorsque l’item est stackable. |

Appeler `SetItemInstance` pour construire le snapshot de présentation et actualiser le widget. `ClearItem` vide les champs et invalide les chargements d’icône en cours. Le widget ne s’abonne pas à un inventaire : le widget propriétaire doit le rafraîchir lorsque son modèle change. Chaque projet peut créer un widget enfant et personnaliser le style ou ajouter des informations propres au jeu.

Le snapshot courant se lit par `GetViewData()` (Blueprint Pure); la propriété `ViewData` reste lisible par les Blueprints enfants mais n’est plus publique. Après chaque `SetItemInstance` ou `ClearItem`, le widget met à jour ses widgets natifs puis déclenche l’événement Blueprint `OnItemViewDataChanged(NewViewData)` : c’est le point d’extension des widgets enfants, sans sondage de `ViewData`. En C++, `RefreshView()` est virtuelle (appeler `Super` pour conserver le comportement par défaut).

`BuildItemViewData` retourne des données de présentation et ne modifie jamais l’instance. Il ne charge pas la définition : si elle n’est pas déjà en mémoire, le helper renvoie `false`. `bHasStack` n’est vrai que si l’item est stackable et porte un état de stack valide. Le propriétaire doit charger cette définition de façon asynchrone puis rappeler `SetItemInstance`. Une fois les données de présentation disponibles, le widget charge lui-même l’icône soft de façon asynchrone. Les autres champs de définition sont accessibles via les requêtes d’ItemCore.

## Drag & drop générique

`FPEPItemDragData` est un payload Blueprint destiné à être stocké dans un `DragDropOperation`. Au démarrage du drag, renseigner l’item, sa localisation source, le décalage de saisie dans la grille et l’orientation de prévisualisation.

Ce payload ne démarre pas le drag, ne calcule pas une destination, ne valide pas un drop et ne mute pas l’inventaire. La source UI garde son contexte de store/session. L’UI destinataire porte la règle métier : placement dans `PEPInventoryUI`, filtre de slot dans `PEPEquipmentUI`.

## Exemple d’intégration

1. Créer un `WBP_PEPItemView` ou un Blueprint enfant dans le projet hôte.
2. À partir de l’instance lue dans le store, appeler `SetItemInstance` sur le widget.
3. Réutiliser ce widget dans une grille, une liste, un panneau de détail ou une prévisualisation de drag.
4. Pour permettre le drag, ajouter le workflow de la UI source et transmettre `FPEPItemDragData`; ne pas confier la mutation à `PEPItemUI`.

Les widgets de conteneur, fenêtres d’inventaire et sessions de transfert restent dans `PEPInventoryUI`.
