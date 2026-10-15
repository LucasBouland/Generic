# PEPEquipment

`PEPEquipment` expose l’apparence publique de l’équipement d’un pion. L’inventaire complet et le contenu privé des conteneurs ne sont jamais projetés.

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
- `GetPublicAttachments()` : enfants visibles directs ou récursifs.

`FindLocalPresentationTarget()` fournit une cible générique pour les acteurs attachés et les parties modulaires. `FindLocalPresentationActor()` reste disponible pour la compatibilité des consommateurs existants.

## Réplication

`PublicEquipmentView` est un Fast Array contenant seulement `ItemId`, `DefinitionId`, slot, main et relation publique parent/attachment. `PostReplicatedReceive` déclenche une reconstruction locale complète et déterministe.

Les props temporaires (`RequestShowTemporaryItemPresentation`) sont eux aussi de l’état répliqué (`TemporaryPresentationView`), pas des RPC : un pion qui devient pertinent reçoit ses props courants, et une demande répétée écrase la précédente au lieu de remplir la file reliable.

Les acteurs, composants et meshes de présentation ne sont pas répliqués. Il n’y a pas de prédiction client : le client demande, le serveur valide via `IPEPItemStore`, puis sa projection confirmée est répliquée.

## Événements d’équipement

`OnItemEquipped` et `OnItemUnequipped` (paramètre `FPEPEquipmentChange` : type, nœud public, slot racine, profondeur) sont levés sur chaque machine, par différence entre la projection précédemment annoncée et la nouvelle :

- sur l’autorité quand `RefreshPublicProjection` modifie la projection ;
- sur chaque client quand le Fast Array répliqué arrive (`HandlePublicEquipmentReplicated`).

L’autorité ne reçoit jamais son propre Fast Array : un listen server annonce une seule fois, chaque client une seule fois. Une réception qui ne change rien n’annonce rien.

L’ordre est déterministe et ne dépend que des deux projections : tous les `Unequipped` (attachments avant leur racine), puis tous les `Equipped` (racines avant attachments), départagés par `ItemId`. Un item qui change de slot produit `Unequipped(ancien slot)` puis `Equipped(nouveau slot)`, et ses attachments sont ré-annoncés sous le nouveau slot racine. Les attachments sont inclus ; un consommateur qui ne veut que les occupants de slot filtre sur `Node.ParentItemId`. Les deux événements précèdent `OnPublicEquipmentChanged`, qui reste inchangé.

Les événements peuvent être levés avant le `BeginPlay` d’un écouteur sur un pion fraîchement répliqué : lire `GetPublicPresentationNodes()` au moment de s’abonner pour rattraper l’état courant. Un écouteur qui modifie lui-même l’équipement pendant la diffusion voit sa modification annoncée dans une passe suivante, jamais au milieu de la précédente.

## Déséquipement

`RequestUnequipItem(ItemId, TargetLocation, OutRequestId)` suit le même chemin que l’équipement : exécution directe sur l’autorité, sinon RPC serveur, puis résolution via `OnEquipmentRequestResolved` (les identifiants de requête sont partagés avec l’équipement). `UnequipItem` est l’exécuteur autorité (Blueprint Authority Only) et `PreviewUnequipItem` la validation locale, symétrique de `PreviewEquipItem`.

- L’item doit occuper directement un slot de ce composant (`Reason.StateMismatch` sinon).
- `TargetLocation` est obligatoire : le contrat `IPEPItemStore` n’offre aucun placement automatique (`AddItem` et `MoveItem` exigent une destination ; `MoveItemAuto` est propre à `PEPInventory` et peut répondre par un autre slot d’équipement). Une location non renseignée échoue avec `Reason.UnsupportedMode`, une location mal formée avec `Reason.InvalidTarget`.
- Une cible qui est elle-même un slot de ce composant est refusée (`Reason.InvalidTarget`) : c’est un équipement, avec ses règles d’échange et d’éligibilité.
- Le déséquipement est toujours un `MoveItem`, jamais un échange : déplacer l’occupant de la cible dans le slot serait un équipement implicite.

## Éligibilité

Un item entre dans un slot si, dans l’ordre (premier refus gagnant) :

1. il respecte les `RequiredItemTags` / `BlockedItemTags` du slot ;
2. `CanEquipItemInSlot` (BlueprintNativeEvent protégé, pour les sous-classes) l’accepte ;
3. chaque objet du propriétaire implémentant `IPEPEquipmentEligibilityProvider` l’accepte : l’acteur propriétaire d’abord, puis ses composants triés par nom.

Cette règle s’applique dans `PreviewEquipItem`, dans le chemin autoritaire d’équipement et dans le validateur de location enregistré sur le store : un déplacement vers un slot depuis une UI d’inventaire, un échange ou un placement automatique y est soumis. Les fournisseurs reçoivent la définition seulement si elle est déjà chargée (jamais de chargement synchrone) et doivent rester des requêtes pures, appelées sur les clients pour les previews et éventuellement plusieurs fois par requête. Un refus sans `ReasonTag` est normalisé en `Reason.BlockedByState`.

## Pont GMAS

Le module runtime `PEPEquipmentGMAS` relie l’équipement au GMC Ability System (dépendance plugin `GMCAbilitySystem`). Le module `PEPEquipment` lui-même ne dépend pas de GMAS.

- `FPEPItemFragment_EquipmentAbilities` (fragment de définition) : `AbilityMaps` (`FAbilityMapData` : InputTag → abilities), `AbilityMapPriority`, `EquippedEffects`, `ActiveInSlots` (slots racines où l’item accorde quelque chose ; vide = tous), `RequiredActiveTags` / `BlockedActiveTags` (éligibilité selon les tags actifs GMAS du porteur) et `bApplyWhenAttachment` (faux par défaut : seuls les occupants de slot accordent).
- `UPEPEquipmentAbilityBridgeComponent` se place sur le pion à côté de `UPEPEquipmentComponent` et `UGMC_AbilitySystemComponent`, suit `OnItemEquipped` / `OnItemUnequipped` et implémente `IPEPEquipmentEligibilityProvider` pour les tags actifs requis/bloqués.

Répartition réseau :

- L’`AbilityMap` de GMAS est une donnée locale non répliquée : les entrées sont écrites sur le serveur et sur chaque client, toujours avec `bGrantedByDefault` à faux pour qu’un client n’accorde jamais localement.
- `GrantedAbilityTags` est lié par GMC en `ServerAuth_Output_ClientValidated` : seule l’autorité appelle `GrantAbilityByTag` / `RemoveGrantedAbilityByTag`, et seulement pour un tag qu’elle a elle-même accordé (un tag déjà accordé par ailleurs n’est jamais retiré par le pont).
- Les `EquippedEffects` sont appliqués par l’autorité en `ServerAuth` et retirés par `RemoveEffectByIdSafe(..., ServerAuth)` avec les identifiants conservés par item. Les changements d’équipement arrivent par RPC serveur, écriture de store ou réplication, hors du cycle de mouvement GMC : ni `Predicted` ni `PredictedQueued` ne conviennent, et rien d’irréversible n’est déclenché dans un chemin de prédiction ou de replay. Pour la même raison, ne jamais émettre de requête d’équipement depuis un tick d’ability prédit.

Collisions d’InputTag : `FPEPEquipmentAbilityMapLedger` garde toutes les contributions par item. Le gagnant d’un InputTag dépend seulement de l’ensemble équipé (`AbilityMapPriority` puis plus petit `ItemId`), jamais de l’ordre d’arrivée, pour que serveur et client s’accordent. Déséquiper un item rend l’entrée à l’item restant ; quand plus rien ne contribue, l’entrée d’origine de l’ASC est restaurée, ou remplacée par une entrée sans ability si l’ASC n’en avait pas (l’API publique de GMAS ne permet pas de retirer une seule entrée).

Le pont attend le `BeginPlay` de l’ASC (qui remplit son `AbilityMap` et écraserait les entrées écrites avant), puis applique l’état déjà équipé. Les définitions non chargées sont demandées en asynchrone et appliquées au chargement si l’item est toujours équipé. En `EndPlay`, si seul le pont disparaît, il rend ce qu’il a accordé ; si tout le pion est détruit, il ne touche plus l’ASC.

## Mains et dégainage

L’API runtime de `PEPEquipment` évolue de façon additive : les ajouts (événements typés, déséquipement, fournisseurs d’éligibilité) ne changent pas le comportement des appels existants. `EPEPEquipmentHand`, `Hand` sur les slots et `GetActiveHandItem` restent comme lecture de la projection (« quel item occupe quelle main »).

`UPEPEquipmentComponent` ne porte plus de logique de main active (dégainer, rengainer, notifies `Equip`/`Unequip`, slot source réservé). Un slot peut déclarer une `Hand` et `GetActiveHandItem` lit la projection, mais choisir quelle arme est en main est une règle de jeu : elle sera réimplémentée dans un composant dédié, au-dessus de celui-ci, configuré par le profil.

La présentation locale est maintenant réconciliée par différence : un visuel dont le nœud public n’a pas changé est conservé, y compris son socket courant. Changer un `PresentationTarget`, le mesh principal ou le profil reconstruit tout.

## Validation

`Validate Assets` détecte notamment les visuels invalides, les overrides dupliqués, les slots/profils en conflit et les sockets ou bones absents pour les attachments. Le code de validation croisée réside dans le module éditeur `PEPEquipmentEditor`.

## UI

Le module runtime `PEPEquipmentUI` fournit les helpers Blueprint propres à l'interface Equipment.
`Filter Equipment Slot Change` indique si une notification de store touche un slot, y compris via
son contenu imbriqué, et si l'UI doit rafraîchir ou vider l'image de l'item racine. Il dépend de
`PEPCoreUI` et `PEPItemCore`, sans ajouter de dépendance Equipment à `PEPItemUI`.

## Tests

Les tests d’automatisation (`PEP.Equipment.*`) utilisent `UPEPTestItemStoreComponent` et `UPEPTestEquipmentListenerComponent` (dans `Private/Tests`). Ce sont des `UCLASS` : UHT ne permet pas de les exclure du build Shipping par un `#if` sans casser le code généré ; ils y restent compilés mais ne sont jamais instanciés hors des tests. Les tests du pont GMAS (`PEP.Equipment.GMAS.*`) couvrent le ledger et le fragment, sans ASC.

## Dépendances

Le module runtime dépend de `PEPItemCore`, jamais de `PEPInventory` : les attachements publics sont demandés au store via `IPEPItemStore::GetPublicChildren`, que chaque store résout avec son propre vocabulaire. Seul le module éditeur `PEPEquipmentEditor` (validation des définitions) lit encore le fragment Container de `PEPInventory`. `UPEPInventoryComponent` est une implémentation possible de `IPEPItemStore`, mais un mannequin de preview, une tourelle ou un store spécialisé peuvent fournir le même contrat.

Seul le module `PEPEquipmentGMAS` dépend de `GMCAbilitySystem` (et, à travers lui, de GMC) ; le plugin déclare cette dépendance pour lui. `PEPEquipment` et `PEPEquipmentUI` n’en utilisent aucun symbole.
