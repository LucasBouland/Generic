# PEPEquipment

`PEPEquipment` expose l’apparence publique de l’équipement d’un pion. L’inventaire complet et le contenu privé des conteneurs ne sont jamais projetés.

À côté de cette projection publique répliquée, l’autorité et le client propriétaire tiennent une **vue propriétaire** locale (voir plus bas) : tous les slots du profil, y compris ceux cachés aux autres joueurs. Les grants GMAS et l’UI du propriétaire la suivent.

## Mise en place

1. Ajouter un objet implémentant `IPEPItemStore` et un `UPEPEquipmentComponent` au pion répliqué.
2. Affecter le mesh principal avec `SetPresentationMesh`.
3. Affecter un `UPEPEquipmentProfile` au composant, ou utiliser temporairement ses slots fallback.
4. Déclarer dans le profil les `PresentationTargets` disponibles et, pour les cibles modulaires,
   leur `ExpectedSkeleton` optionnel.
5. Enregistrer les composants réels avec `RegisterPresentationTarget` sur le pion.
6. Ajouter un `FPEPEquipmentPresentationFragment` typé aux définitions visibles.

Les designers choisissent dans `Visual` une seule variante : `Static Mesh`, `Skeletal Mesh` ou `No Visual`. Les anciens champs `StaticMesh` et `SkeletalMesh` ne servent qu’à migrer les assets existants.

## Profils et présentation

Chaque slot du profil utilise une politique :

- `Attached` crée un acteur cosmétique local et l’attache au socket configuré ;
- `ModularSkeletal` applique le Skeletal Mesh sur une cible enregistrée du pion.

Tout `PresentationTargetTag` utilisé par un slot doit être déclaré dans le même profil. Cette
déclaration décrit le contrat designer ; le pion reste responsable de fournir le composant réel.

Le pion reste propriétaire de son apparence de base. Equipment conserve seulement un override temporaire et restaure le mesh de base au déséquipement. Les attachments visibles sont reconstruits récursivement depuis les slots de conteneur de type `Attachment`.

Les APIs publiques distinguent :

- `GetPublicEquipmentRoots()` : items occupant directement un slot Equipment ;
- `GetPublicPresentationNodes()` : racines et attachments visibles ;
- `GetPublicAttachments()` : enfants visibles directs ou récursifs ;
- `FindPublicPresentationNode()` : un nœud (racine ou attachment) par `ItemId`, en temps constant.

Getters et événements renvoient `FPEPEquippedItemInfo`, une valeur simple (`ItemId`, `DefinitionId`, `SlotTag`, `ParentItemId`, `ParentSlotName`). `FPEPEquippedItemPublic` est l’élément interne du Fast Array répliqué et n’est plus exposé aux Blueprints.

`FindLocalPresentationTarget()` fournit une cible générique pour les acteurs attachés et les parties modulaires ; l’acteur d’un visuel attaché est `PresentationActor` de cette cible.

La présentation locale n’est jamais construite avant le `BeginPlay` du composant : un Fast Array reçu dans le premier bunch d’un late joiner, ou un store rempli sur l’autorité avant `BeginPlay`, aurait sinon accroché ses visuels à la racine (la capsule) faute de mesh découvert. `BeginPlay` découvre le mesh, détruit tout visuel éventuel puis reconstruit la projection (autorité) et la présentation une seule fois. Les props temporaires suivent la même règle.

Un acteur `APEPEquippedPresentationActor` présente un seul nœud pendant toute sa vie : `OnPublicStateApplied` est levé une fois, depuis `InitializePresentation`, avant le chargement du visuel ; un nœud qui change (slot, parent, définition) reçoit un nouvel acteur. Un prop temporaire reçoit un état dont seul `DefinitionId` est renseigné.

## Réplication

`PublicEquipmentView` est un Fast Array contenant seulement `ItemId`, `DefinitionId`, slot et relation publique parent/attachment. La main n’est pas répliquée : chaque machine la déduit du slot via le même profil. `PostReplicatedReceive` met à jour l’index `ItemId → entrée` puis réconcilie la présentation locale par différence.

Les props temporaires (`RequestShowTemporaryItemPresentation`) sont eux aussi de l’état répliqué (`TemporaryPresentationView`), pas des RPC : un pion qui devient pertinent reçoit ses props courants, et une demande répétée écrase la précédente au lieu de remplir la file reliable. Le serveur borne cet état :

- au plus `MaxTemporaryPresentations` clés vivantes (4 par défaut, réglable sur le composant) ; une nouvelle clé au-delà est refusée, remplacer une clé vivante reste permis ;
- l’item doit être dans le store du composant et, si `TemporaryPresentationAccessTags` n’est pas vide, atteignable pour au moins un de ces accès (`IPEPItemStore::HasGrantedGameplayAccess`). **Vide par défaut** : tout item du store du propriétaire convient (toujours soumis au plafond et au socket). Equipment ne définit aucun vocabulaire d’accès ; le filtre `Categories="Inventory.GameplayAccess"` n’est qu’une chaîne pour le sélecteur, et un projet PEPInventory y met les accès que déclare `PEPInventoryTags` (`Inventory.GameplayAccess.Equip`, `.Use`) ou les siens ;
- le socket doit exister sur le mesh quand la machine en a un.

Les refus sont silencieux et bon marché (cap, socket puis store), sans rien écrire. Le client propriétaire applique les mêmes règles avant d’afficher son prop immédiat ; il ne diverge du serveur que sur une course, et le prop local disparaît alors au prochain changement répliqué de la vue. Les chargements d’un prop temporaire partagent les ensembles d’échecs de la présentation normale : un asset introuvable n’est demandé qu’une fois, et le handle n’est gardé que tant que le chargement est en cours.

Les acteurs, composants et meshes de présentation ne sont pas répliqués. Il n’y a pas de prédiction client : le client demande, le serveur valide via `IPEPItemStore`, puis sa projection confirmée est répliquée.

## Reconstruction différée

Sur l’autorité, chaque événement de store qui peut toucher un slot (caché ou non) marque la projection comme sale ; elle est reconstruite une seule fois au tick moteur suivant (core ticker, qui tourne aussi monde en pause), quel que soit le nombre d’écritures. Le même parcours du store reconstruit la vue propriétaire. Un `StateChanged` (charges, munitions, durabilité) ne reconstruit rien, sauf si un nœud de l’une des deux vues change de définition : les vues ne portent aucun état d’item et le caractère public d’un enfant dépend de son emplacement (`GetPublicChildren`).

Sur le client propriétaire, la vue propriétaire suit la même règle à partir de sa copie du store (PEPInventory réplique l’inventaire au seul propriétaire et lève ces événements sur cette copie) : un seul recalcul au tick suivant. Une réception de la projection publique ou un changement de contrôleur du pion déclenchent aussi un recalcul différé.

Les requêtes exécutées par le composant (`RequestEquipItem`, `RequestUnequipItem`, `UnequipItem` sur l’autorité) regroupent leurs écritures (un échange lève deux événements) et reconstruisent la projection et la vue propriétaire avant de rendre la main. Un code serveur qui écrit directement dans le store et doit lire la projection dans la même frame appelle `FlushDeferredUpdates()`.

Les fins de chargement d’assets de présentation demandent de même une seule réconciliation au tick suivant, et `RegisterPresentationTarget` / `SetPresentationMesh` ne reconstruisent qu’une fois, et seulement si quelque chose a changé.

## Événements d’équipement

`OnItemEquipped` et `OnItemUnequipped` (paramètre `FPEPEquipmentChange` : type, nœud public `FPEPEquippedItemInfo`, slot racine, profondeur) sont levés sur chaque machine, par différence entre la projection précédemment annoncée et la nouvelle :

- sur l’autorité quand la reconstruction (différée ou de fin de requête) modifie la projection ;
- sur chaque client quand le Fast Array répliqué arrive.

Seuls les slots publics y participent : un item dans un slot dont `bShowPublicly` est faux ne lève aucun de ces deux événements (voir la vue propriétaire).

L’autorité ne reçoit jamais son propre Fast Array : un listen server annonce une seule fois, chaque client une seule fois. Une réception qui ne change rien n’annonce rien.

L’ordre est déterministe et ne dépend que des deux projections : tous les `Unequipped` (attachments avant leur racine), puis tous les `Equipped` (racines avant attachments), départagés par `ItemId`. Un item qui change de slot produit `Unequipped(ancien slot)` puis `Equipped(nouveau slot)`, et ses attachments sont ré-annoncés sous le nouveau slot racine. Les attachments sont inclus ; un consommateur qui ne veut que les occupants de slot filtre sur `Node.ParentItemId`. Les deux événements précèdent `OnPublicEquipmentChanged`, qui reste inchangé.

Les événements peuvent être levés avant le `BeginPlay` d’un écouteur sur un pion fraîchement répliqué : lire `GetPublicPresentationNodes()` au moment de s’abonner pour rattraper l’état courant. Un écouteur qui modifie lui-même l’équipement pendant la diffusion voit sa modification annoncée dans une passe suivante, jamais au milieu de la précédente.

## Vue propriétaire

L’inventaire est répliqué au seul propriétaire : l’autorité et le client propriétaire détiennent donc tout le store et peuvent calculer chaque item équipé, y compris dans un slot caché (`bShowPublicly` faux). La vue propriétaire est ce calcul, local et **jamais répliqué** :

- `HasOwnerEquipmentView()` : vrai sur l’autorité et sur le client propriétaire (proxy autonome, ou pion dont le net owner est local) ; un proxy simulé ne garde que la projection publique et sa vue propriétaire reste vide ;
- `GetOwnerEquipmentNodes()` / `FindOwnerEquipmentNode()` : l’occupant de chaque slot du profil, slots cachés compris, puis ses attachments visibles selon la même règle que la projection (`GetPublicChildren`, jamais le contenu privé) ; racines dans l’ordre du profil ;
- `FindEquippedRootInSlot(SlotTag)` : l’occupant d’un slot lu dans la vue la plus complète de la machine — la vue propriétaire là où elle existe, la projection publique ailleurs. C’est ce que lit l’UI ;
- `OnOwnerItemEquipped` / `OnOwnerItemUnequipped` (même `FPEPEquipmentChange`, même diff déterministe que les événements publics), puis `OnOwnerEquipmentChanged`.

Sur l’autorité, une reconstruction met à jour les deux vues avant d’annoncer quoi que ce soit, puis lève les événements publics, puis les événements propriétaire. Le client propriétaire annonce les siens au tick qui suit la modification de sa copie du store. Lire `GetOwnerEquipmentNodes()` au moment de s’abonner pour rattraper l’état courant.

## Déséquipement

`RequestUnequipItem(ItemId, TargetLocation, OutRequestId)` suit le même chemin que l’équipement : exécution directe sur l’autorité, sinon RPC serveur, puis résolution via `OnEquipmentRequestResolved` (les identifiants de requête sont partagés avec l’équipement). `UnequipItem` est l’exécuteur autorité (Blueprint Authority Only) et `PreviewUnequipItem` la validation locale, symétrique de `PreviewEquipItem`.

- L’item doit occuper directement un slot de ce composant (`Reason.StateMismatch` sinon).
- `TargetLocation` est obligatoire : le contrat `IPEPItemStore` n’offre aucun placement automatique (`AddItem` et `MoveItem` exigent une destination ; `MoveItemAuto` est propre à `PEPInventory` et peut répondre par un autre slot d’équipement). Une location non renseignée échoue avec `Reason.UnsupportedMode`, une location mal formée avec `Reason.InvalidTarget`.
- Une cible qui est elle-même un slot de ce composant est refusée (`Reason.InvalidTarget`) : c’est un équipement, avec ses règles d’échange et d’éligibilité.
- Le déséquipement est toujours un `MoveItem`, jamais un échange : déplacer l’occupant de la cible dans le slot serait un équipement implicite.

## Requêtes et identité

Une requête est résolue sur la machine qui l’a émise, et les identifiants sont propres à chaque machine :

- émise sur l’autorité (code serveur, IA, hôte listen server) : la valeur de retour est le résultat (`Executed`/`Failed`) et `OnEquipmentRequestResolved` est levé localement **au tick suivant**, pour qu’un appelant (Blueprint compris) détienne déjà `OutRequestId` à son arrivée. Elle n’est jamais envoyée au client propriétaire, dont les identifiants entreraient en collision ;
- envoyée par le client propriétaire : `Submitted`, puis `Client_ResolveEquipRequest` vers ce client seulement.

`RequestEquipItem` et `PreviewEquipItem` n’exigent un accès de gameplay que si `EquipAccessTags` n’est pas vide (au moins un accès accordé par `IPEPItemStore::HasGrantedGameplayAccess`). **Vide par défaut** : seules l’éligibilité du slot et les règles de déplacement/échange du store s’appliquent. Un projet PEPInventory qui veut interdire d’équiper un item enfoui dans un sac y met `Inventory.GameplayAccess.Equip`.

Le store est interrogé au nom du contrôleur du propriétaire quand il y en a un. Sans contrôleur, une requête locale sur l’autorité est un changement piloté par le serveur (pion non possédé, mannequin, loadout scripté) et utilise délibérément `FPEPItemAccessContext::System()` ; une requête arrivée par RPC client n’est jamais faite en `System()` et est refusée (`Reason.NotAuthority`). Une preview client sans contrôleur interroge en `From(nullptr)`, qui n’est jamais système.

## Éligibilité

Un item entre dans un slot si, dans l’ordre (premier refus gagnant) :

1. il respecte les `RequiredItemTags` / `BlockedItemTags` du slot ;
2. `CanEquipItemInSlot` (BlueprintNativeEvent protégé, pour les sous-classes) l’accepte ;
3. chaque objet du propriétaire implémentant `IPEPEquipmentEligibilityProvider` l’accepte : l’acteur propriétaire d’abord, puis ses composants implémenteurs triés par nom (filtrés avant le tri ; un seul implémenteur n’est jamais trié).

Cette règle s’applique dans `PreviewEquipItem`, dans le chemin autoritaire d’équipement et dans le validateur de location enregistré sur le store : un déplacement vers un slot depuis une UI d’inventaire, un échange ou un placement automatique y est soumis. Les fournisseurs reçoivent la définition seulement si elle est déjà chargée (jamais de chargement synchrone) et doivent rester des requêtes pures, appelées sur les clients pour les previews et éventuellement plusieurs fois par requête. Un refus sans `ReasonTag` est normalisé en `Reason.BlockedByState`.

La liste filtrée et ordonnée des fournisseurs est mise en cache par composant. Le cache est reconstruit quand le propriétaire change, quand son nombre de composants change, ou quand un fournisseur mis en cache a disparu. Seul un échange de composants dans la même frame qui garde le même nombre et n’emporte aucun fournisseur lui échappe : appeler alors `InvalidateEligibilityProviders()` (Blueprint compris). L’ordre reste déterministe : le propriétaire, puis les composants par nom.

## Pont GMAS

Le module runtime `PEPEquipmentGMAS` relie l’équipement au GMC Ability System (dépendance plugin `GMCAbilitySystem`). Le module `PEPEquipment` lui-même ne dépend pas de GMAS.

- `FPEPItemFragment_EquipmentAbilities` (fragment de définition) : `AbilityMaps` (`FAbilityMapData` : InputTag → abilities), `AbilityMapPriority`, `EquippedEffects`, `ActiveInSlots` (slots racines où l’item accorde quelque chose ; vide = tous), `RequiredActiveTags` / `BlockedActiveTags` (éligibilité selon les tags actifs GMAS du porteur) et `bApplyWhenAttachment` (faux par défaut : seuls les occupants de slot accordent).
- `UPEPEquipmentAbilityBridgeComponent` se place sur le pion à côté de `UPEPEquipmentComponent` et `UGMC_AbilitySystemComponent`, suit `OnOwnerItemEquipped` / `OnOwnerItemUnequipped` et implémente `IPEPEquipmentEligibilityProvider` pour les tags actifs requis/bloqués.

**Les grants suivent la vue PROPRIÉTAIRE et ne dépendent pas de `bShowPublicly`.** Un item accorde tant qu’il occupe un slot du profil (ou y pend comme attachment visible avec `bApplyWhenAttachment`), que ce slot soit public ou caché : un slot caché ne retire que le visuel et les événements publics aux autres joueurs. Le contenu privé d’un conteneur n’accorde toujours rien. Pour restreindre où un item accorde, utiliser `ActiveInSlots`. `GatherGrantSourceChanges` et `DoesChangeGrant` (statiques, purs) exposent ce choix pour les tests et les outils.

Répartition réseau :

- L’`AbilityMap` de GMAS est une donnée locale non répliquée : les entrées sont écrites sur l’autorité et sur le client propriétaire (rôle autonome), toujours avec `bGrantedByDefault` à faux pour qu’un client n’accorde jamais localement. Ces deux machines sont exactement celles qui tiennent la vue propriétaire. Un proxy simulé (le pion d’un autre joueur) n’active jamais d’ability depuis une entrée : il n’a pas de vue propriétaire, le pont n’y fait rien et rend ce qu’il aurait écrit. Un changement de contrôleur (possession après le spawn, dépossession) déclenche une réconciliation au tick suivant.
- `GrantedAbilityTags` est lié par GMC en `ServerAuth_Output_ClientValidated` : seule l’autorité appelle `GrantAbilityByTag` / `RemoveGrantedAbilityByTag`, et seulement pour un tag qu’elle a elle-même accordé (un tag déjà accordé par ailleurs n’est jamais retiré par le pont).
- Les `EquippedEffects` sont appliqués par l’autorité en `ServerAuth` et retirés par `RemoveEffectByIdSafe(..., ServerAuth)` avec les identifiants conservés par item. Les changements d’équipement arrivent par RPC serveur, écriture de store ou réplication, hors du cycle de mouvement GMC : ni `Predicted` ni `PredictedQueued` ne conviennent, et rien d’irréversible n’est déclenché dans un chemin de prédiction ou de replay. Pour la même raison, ne jamais émettre de requête d’équipement depuis un tick d’ability prédit.

Collisions d’InputTag : `FPEPEquipmentAbilityMapLedger` garde toutes les contributions par item. Le gagnant d’un InputTag dépend seulement de l’ensemble équipé (`AbilityMapPriority` puis plus petit `ItemId`), jamais de l’ordre d’arrivée, pour que serveur et client s’accordent. Déséquiper un item rend l’entrée à l’item restant ; quand plus rien ne contribue, l’entrée d’origine de l’ASC est restaurée, ou remplacée par une entrée sans ability si l’ASC n’en avait pas (l’API publique de GMAS ne permet pas de retirer une seule entrée).

Le pont attend le `BeginPlay` de l’ASC (qui remplit son `AbilityMap` et écraserait les entrées écrites avant), puis applique l’état déjà équipé. Les définitions non chargées sont demandées en asynchrone et appliquées au chargement si l’item est toujours équipé. En `EndPlay`, si seul le pont disparaît, il rend ce qu’il a accordé ; si tout le pion est détruit, il ne touche plus l’ASC.

## Mains et dégainage

`EPEPEquipmentHand` et `Hand` sur les slots du profil restent comme lecture de la projection (« quel item occupe quelle main »). La main n’est plus un champ répliqué : `GetSlotHand(SlotTag)` la lit dans le profil effectif, et `FindItemInHand(Hand)` (anciennement `GetActiveHandItem`) renvoie la racine publique du premier slot, **dans l’ordre du profil**, qui déclare cette main — donc la même réponse sur chaque machine, même avec un profil invalide donnant une main à deux slots.

`UPEPEquipmentComponent` ne porte plus de logique de main active (dégainer, rengainer, notifies `Equip`/`Unequip`, slot source réservé). Choisir quelle arme est en main est une règle de jeu : elle sera réimplémentée dans un composant dédié, au-dessus de celui-ci, configuré par le profil.

La présentation locale est réconciliée par différence : un visuel dont le nœud public n’a pas changé est conservé, y compris son socket courant ; un nœud invalidé emporte ses descendants en un seul parcours. Changer un `PresentationTarget`, le mesh principal ou le profil reconstruit tout, une fois.

## Migration (API)

Changements cassants de cette révision, sans consommateur dans le dépôt :

| Avant | Après |
| --- | --- |
| `FPEPEquippedItemPublic` dans les getters, `FPEPEquipmentChange::Node`, `APEPEquippedPresentationActor::PublicState` / `OnPublicStateApplied` | `FPEPEquippedItemInfo` (mêmes champs, sans `Hand`) |
| `FPEPEquippedItemPublic::Hand` (répliqué) | `GetSlotHand(SlotTag)` |
| `GetActiveHandItem` | `FindItemInHand` (CoreRedirect) |
| `FindPublicEquippedItem` | `FindPublicPresentationNode` (CoreRedirect) |
| `FindLocalPresentationActor` | `FindLocalPresentationTarget(...).PresentationActor` |
| `UPEPEquipmentProfile::FindSlot`, `APEPEquippedPresentationActor::ApplyPublicState` / `MatchesPresentationNode` | supprimés (inutilisés) |
| `HandlePublicEquipmentReplicated` / `HandleTemporaryPresentationsReplicated` publics | privés (appelés par les vues Fast Array, amies) |
| `FPEPPublicEquipmentView` `BlueprintType`, `OwnerComponent` public | ni `BlueprintType` ni membre public |

Changements de comportement de la révision suivante :

| Avant | Après |
| --- | --- |
| Grants GMAS depuis la projection publique ; un slot caché n’accorde rien | Grants depuis la vue propriétaire ; un slot caché accorde comme les autres |
| Avertissement de validation pour chaque slot `bShowPublicly` faux | Aucun avertissement |
| `TemporaryPresentationAccessTags` = Equip, Reload, Use par défaut | Vide par défaut (tout item du store) |
| `RequestEquipItem` / `PreviewEquipItem` exigent l’accès `Inventory.GameplayAccess.Equip` (constante de `PEPItemTags`, supprimée) | Exigent `EquipAccessTags` seulement s’il n’est pas vide (vide par défaut) |
| Liste des fournisseurs d’éligibilité recalculée à chaque requête | Mise en cache par composant, `InvalidateEligibilityProviders()` |

Les CoreRedirects (`Config/DefaultEngine.ini`) gardent les nœuds Blueprint existants ; le type de leur broche de sortie change (`FPEPEquippedItemInfo`), et une liaison vers un « Break » de l’ancien type est à refaire.

## Validation

`Validate Assets` détecte notamment les visuels invalides, les overrides dupliqués, les slots/profils en conflit et les sockets ou bones absents pour les attachments. Le code de validation croisée réside dans le module éditeur `PEPEquipmentEditor`. Les sockets d’attachment ne sont vérifiés que contre un mesh nommé par le fragment : un visuel `Actor` sans `MeshVisual` apporte sa géométrie dans sa propre classe et n’est pas (faussement) refusé. Un slot dont `bShowPublicly` est faux n’est plus signalé : c’est un choix légitime qui n’ôte que la présence publique (l’occupant reste dans la vue propriétaire et accorde ses abilities).

L’avertissement « no item store found » (et « claimed no slot ») n’est émis qu’après `BeginPlay` dans un monde de jeu : `OnRegister` tourne aussi dans les mondes d’éditeur et de preview, et avant que tous les composants du propriétaire existent.

## UI

Le module `PEPEquipmentUI` est l’interface du domaine Equipment. Il dépend de `PEPEquipment`, de `PEPItemUI` (`FPEPItemViewData`, `UPEPItemUILibrary`, `UPEPItemViewWidget`) et de `PEPCoreUI` (`UPEPDropTargetWidget`, états visuels), sur CommonUI/UMG. Le module gameplay `PEPEquipment` ne dépend toujours d’aucune UI, et `PEPItemUI` ne dépend pas d’Equipment.

### `UPEPEquipmentSlotWidget`

Classe de base abstraite (`UCommonUserWidget`, Blueprintable) d’un slot d’équipement à l’écran. Aucun WBP n’est encore livré.

- Configuration : `SlotTag` (éditable, ou `SetSlotTag`), puis `BindToEquipment(Component)` / `Unbind()`. Le composant est tenu en référence faible ; le widget ne porte aucune vérité métier.
- Lecture : `UPEPEquipmentComponent::FindEquippedRootInSlot`, donc la vue propriétaire sur l’autorité et le client propriétaire (ses slots cachés s’affichent pour lui) et la projection publique ailleurs. `GetEquippedItemInfo`, `GetItemViewData` (`FPEPItemViewData` construit par PEPItemUI : instance complète du store quand la machine la détient, sinon instantané `ItemId` + `DefinitionId`), `GetSlotDisplayData` (libellé, main, visibilité depuis le profil).
- Rafraîchissement : abonné entre `NativeConstruct` et `NativeDestruct` à `OnPublicEquipmentChanged`, `OnOwnerEquipmentChanged` et aux changements du store qui touchent directement ce slot (`FilterEquipmentSlotChange` : pile, durabilité). `OnSlotItemChanged(bHasItem, ItemInfo, ViewData)` (BlueprintImplementableEvent, précédé du hook natif `NativeOnSlotItemChanged`) n’est levé que si ce qui est affiché a changé ; une construction l’émet toujours une fois. Un lien fait avant la construction est mémorisé et suivi dès `NativeConstruct`. Une définition non résidente est chargée en asynchrone (jamais de chargement synchrone, une seule tentative par définition et par widget) puis le slot se rafraîchit.
- Glisser-déposer : le widget n’interprète aucun payload (le WBP extrait l’`ItemId` du `DragDropOperation` de son projet ou de `PEPInventoryUI`). `CanAcceptItem(ItemId)` → `PreviewEquipItem`, `AcceptItem(ItemId)` → `RequestEquipItem`, `CanUnequipTo(Target)` → `PreviewUnequipItem`, `RequestUnequipTo(Target)` → `RequestUnequipItem` de l’occupant courant. `UpdateDropPreview(ItemId)` / `ClearDropPreview()` pilotent le `DropTarget` optionnel (`BindWidgetOptional`, `UPEPDropTargetWidget` de PEPCoreUI : Hovered, Valid, Invalid, Neutral). Un `ItemView` optionnel (`UPEPItemViewWidget`) reçoit l’item ou est vidé.

### `UPEPEquipmentUILibrary`

- `Filter Equipment Slot Change` indique si une notification de store touche un slot, y compris via son contenu imbriqué, et si l’UI doit rafraîchir ou vider l’image de l’item racine.
- `GetEquipmentSlotDisplayData` / `MakeDefaultSlotLabel` : libellé (`DisplayName` du slot dans le profil, sinon dernier segment du tag), main et `bShowPublicly`.
- `MakeEquippedItemSnapshot` / `BuildEquippedItemViewData` : l’item à présenter pour un nœud équipé, et ses `FPEPItemViewData`.
- `GetDropTargetVisualState` : verdict d’équipement → état visuel PEPCoreUI.

`FPEPEquipmentSlotDefinition::DisplayName` (optionnel, présentation seulement, jamais répliqué) donne son libellé à un slot.

### Type de module

`PEPEquipmentUI` reste `Runtime`, comme `PEPItemUI`, `PEPInventoryUI` et `PEPPlayerUI`. `ClientOnly` l’exclurait des cibles serveur dédié, mais ses dépendances UI y sont compilées de toute façon, et un module ou un asset du projet qui le référencerait casserait la cible serveur. Le passage à `ClientOnly` est à faire, s’il est voulu, pour tous les modules UI PEP ensemble.

## Tests

Les tests d’automatisation (`PEP.Equipment.*`) utilisent `UPEPTestItemStoreComponent` et `UPEPTestEquipmentListenerComponent` (dans `Private/Tests`). Le monde de test n’a pas de game mode : les fixtures appellent `DispatchBeginPlay` sur le propriétaire, puis `FlushDeferredUpdates` après chaque écriture directe dans le store, puisque rien n’y fait avancer le tick. Ce sont des `UCLASS` : UHT ne permet pas de les exclure du build Shipping par un `#if` sans casser le code généré ; ils y restent compilés mais ne sont jamais instanciés hors des tests. `UPEPTestItemStoreComponent` est exporté (`PEPEQUIPMENT_API`) pour que les modules de test voisins incluent son en-tête privé par chemin relatif.

- `PEP.Equipment.OwnerView.*` : la vue propriétaire contient les slots cachés que la projection publique omet, ses événements, son regroupement par tick et par requête.
- `PEP.Equipment.Eligibility.ProviderCacheFollowsTheOwnersComponents` : invalidation du cache des fournisseurs.
- `PEP.Equipment.GMAS.*` : le ledger et le fragment, sans ASC, et le choix de la source des grants (`GatherGrantSourceChanges` lit la vue propriétaire, slot caché compris).
- `PEP.Equipment.UI.*` : le filtre de changements de store, la bibliothèque UI et `UPEPEquipmentSlotWidget` instancié par `NewObject`, sans Slate ni viewport (`UPEPTestEquipmentSlotWidget` suit les événements sans `NativeConstruct`). Le chemin proxy simulé n’y est couvert qu’au niveau bibliothèque (instantané d’un nœud que la machine ne détient pas) : un monde de test standalone est toujours autorité.

Les tests n’utilisent aucune constante de `PEPItemTags` : les tags propres aux tests sont des tags natifs locaux sous `Test.`.

## Dépendances

Le module runtime dépend de `PEPItemCore`, jamais de `PEPInventory` : les attachements publics sont demandés au store via `IPEPItemStore::GetPublicChildren`, que chaque store résout avec son propre vocabulaire. Seul le module éditeur `PEPEquipmentEditor` (validation des définitions) lit encore le fragment Container de `PEPInventory`. `UPEPInventoryComponent` est une implémentation possible de `IPEPItemStore`, mais un mannequin de preview, une tourelle ou un store spécialisé peuvent fournir le même contrat.

Seul le module `PEPEquipmentGMAS` dépend de `GMCAbilitySystem` (et, à travers lui, de GMC) ; le plugin déclare cette dépendance pour lui. `PEPEquipment` et `PEPEquipmentUI` n’en utilisent aucun symbole.

`PEPEquipmentUI` dépend de `PEPEquipment`, `PEPItemUI`, `PEPItemCore`, `PEPCoreUI`, `CommonUI` et `UMG` ; le plugin déclare `PEPItems`, `PEPCoreUI` et `CommonUI`.
