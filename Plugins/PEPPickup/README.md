# Objets au sol et ouverture

## Stockage

- `APEPItemWorldActor` est une base abstraite de présentation, sans stockage d’arbre.
- `APEPSimpleItemWorldActor` conserve une seule `FPEPItemInstance` serveur. `APEPPickupItemActor` ajoute l’interaction Pickup.
- `APEPContainerItemWorldActor` conserve la racine et ses descendants uniquement dans son `UPEPInventoryComponent`. `APEPPickupContainerActor` ajoute Open et Pickup.
- Un fragment Container, même vide ou composé uniquement de slots Attachment, sélectionne la classe container.
- Les arbres retournés par `GetAuthorityItemTree` sont des snapshots temporaires pour les transactions ; aucune seconde copie persistante n’est conservée.

## Placement et drop

`BP_PEP_Item_Spawner` dérive de `APEPItemWorldSpawner`. Placer ce Blueprint dans un niveau et régler Spawn Item (Direct) ou Spawn Preset. Il choisit la classe configurée dans PEP Items, crée l’instance au démarrage côté serveur puis disparaît. Le drop utilise les mêmes deux classes configurées.

Pour un placement direct, utiliser `BP_PEP_Item_WorldActor` pour un item simple et `BP_PEP_Container_WorldActor` pour un Container. La validation d’asset signale une définition incompatible. Un drop vers une classe incompatible conserve l’item dans le stockage source.

## Open et UI

Le point d’entrée joueur reste `RequestInteractWithInteractionTag(Interaction.Open)`. Le serveur ouvre une session vers le stockage du container sur le `UPEPInventorySessionComponent` du PlayerController de l’interacteur : ce composant doit être ajouté au PlayerController du projet (il résout seul l’inventaire du pion possédé, ou celui que `SetLocalInventory` lui désigne). Un pion sans contrôleur joueur, un bot par exemple, ne peut pas ouvrir de session. La distance d’ouverture est réglable sur l’acteur (300 cm par défaut).

`PEPInventoryUISubsystem` enregistre automatiquement l’inventaire du pawn à la possession (l’enregistrement explicite reste disponible pour un autre emplacement). `SessionWindowClass` dans Inventory UI désigne la fenêtre existante `WBP_PEP_Window_Item_Inventory`. Celle-ci reçoit OwnerItem et InventoryContext, puis lit les slots dans la vue correspondant au contexte. Les droits de lecture/transfert restent ceux des containers ; Open ne les contourne pas.

Une couche Canvas héberge la fenêtre. Échap ferme la fenêtre et demande la fermeture de session. Le ramassage de la racine invalide les sessions des autres joueurs. Le contenu privé du composant au sol n’est pas répliqué ; chaque joueur ne reçoit que sa vue autorisée. Les modifications d’attachments actualisent la présentation publique.

## Migration et vérification

`Tools/migrate_world_item_storage.py` est une migration explicite des Blueprints du projet, déjà appliquée. L’outil éditeur PEPPickupEditor adapte leurs lectures aux contextes et relie le rafraîchissement de session. Il ne modifie aucun asset automatiquement au chargement.

Tests automatisés : `PEP.Items.WorldActor`, `PEP.Inventory`, `PEP.Pickup`. Le test `PEP.Pickup.WorldStorage.OpenTransferAndDrop` couvre les deux stockages, les containers vides/verrouillés, deux sessions simultanées, les modifications d’attachments et les drops refusés sans perte.

## Type de panneau de loot

Sur le `PEPInventoryComponent` de la cible, régler `Loot Panel Tag` avec un tag `Inventory.LootPanel.*` défini par le projet (par exemple `Inventory.LootPanel.Corpse`). Le serveur copie ce tag à l’ouverture de session et le réplique uniquement au joueur concerné. Le plugin ne définit aucun tag de panneau lui-même.

Dans le Blueprint de l’écran, appeler `Get Transfer Session Panel Tag` sur `PEPInventoryUISubsystem`, puis sélectionner la classe du LootPanel. Un tag invalide signifie qu’aucune session n’est disponible ou que la cible n’en demande aucun : attendre une notification de session avant de créer un panneau. Le même getter existe sur `PEPInventoryComponent`.

Le type reste stable pendant la session ; modifier la cible prend effet à sa prochaine ouverture. L’arrivée réseau du type déclenche `OnSessionViewChanged`. Les widgets doivent donc aussi réagir à cet événement, et pas uniquement à `OnTransferSessionPresented`. Le mapping tag vers classes de widgets reste à configurer dans le Blueprint UI.
