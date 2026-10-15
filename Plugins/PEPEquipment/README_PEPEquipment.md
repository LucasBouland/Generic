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

## Mains et dégainage

L’API runtime de `PEPEquipment` est figée : seuls des helpers Blueprint additifs peuvent s’y ajouter. `EPEPEquipmentHand`, `Hand` sur les slots et `GetActiveHandItem` restent comme lecture de la projection (« quel item occupe quelle main »).

`UPEPEquipmentComponent` ne porte plus de logique de main active (dégainer, rengainer, notifies `Equip`/`Unequip`, slot source réservé). Un slot peut déclarer une `Hand` et `GetActiveHandItem` lit la projection, mais choisir quelle arme est en main est une règle de jeu : elle sera réimplémentée dans un composant dédié, au-dessus de celui-ci, configuré par le profil.

La présentation locale est maintenant réconciliée par différence : un visuel dont le nœud public n’a pas changé est conservé, y compris son socket courant. Changer un `PresentationTarget`, le mesh principal ou le profil reconstruit tout.

## Validation

`Validate Assets` détecte notamment les visuels invalides, les overrides dupliqués, les slots/profils en conflit et les sockets ou bones absents pour les attachments. Le code de validation croisée réside dans le module éditeur `PEPEquipmentEditor`.

## UI

Le module runtime `PEPEquipmentUI` fournit les helpers Blueprint propres à l'interface Equipment.
`Filter Equipment Slot Change` indique si une notification de store touche un slot, y compris via
son contenu imbriqué, et si l'UI doit rafraîchir ou vider l'image de l'item racine. Il dépend de
`PEPCoreUI` et `PEPItemCore`, sans ajouter de dépendance Equipment à `PEPItemUI`.

## Dépendances

Le module runtime dépend de `PEPItemCore`, jamais de `PEPInventory` : les attachements publics sont demandés au store via `IPEPItemStore::GetPublicChildren`, que chaque store résout avec son propre vocabulaire. Seul le module éditeur `PEPEquipmentEditor` (validation des définitions) lit encore le fragment Container de `PEPInventory`. `UPEPInventoryComponent` est une implémentation possible de `IPEPItemStore`, mais un mannequin de preview, une tourelle ou un store spécialisé peuvent fournir le même contrat.
