# PEPItems

PEPItems fournit le modèle générique des objets du jeu : définitions, instances, fragments de données et opérations sur l’état des items. Les plugins de domaine consomment ces contrats et ajoutent leurs propres fragments et règles sans faire dépendre ItemCore de leurs modules.

## Responsabilités actuelles

| Module | Responsabilité |
| --- | --- |
| `PEPItemCore` | `UPEPItemDefinition`, identifiants et instances, fragments génériques (stack, ressource, durabilité, poids, présentation), état, requêtes, règles, mutations et vocabulaire de localisation de store. |
| `PEPItemUI` | Présentation réutilisable d’un item : données d’affichage, payload de drag générique et `WBP_PEPItemView`. Aucun accès ou placement dans un inventaire. |
| `PEPItemCoreEditor` | Personnalisation et validation des définitions d’items génériques. |
| `PEPInventory` | Arbres d’items, conteneurs et slots, forme de grille, presets avec placement, acteurs d’items dans le monde et réglages d’inventaire. |
| `PEPInventoryUI` | Workflow UI d’inventaire : contextes source/destination, prévisualisation et demandes de placement, fenêtres et sessions. |
| `PEPInventoryEditor` | Personnalisations d’assets d’inventaire et navigateur de définitions actuellement rendu en Slate. Le futur navigateur Blueprint décrit dans `README_PEPInventoryEditor.md` couvrira aussi les acteurs du niveau. |
| `PEPEquipment` | Fragment `Equippable`, requêtes et contrats de slots d’équipement. Il dépend de ItemCore et Inventory; ces modules ne dépendent pas d’Equipment. |
| `PEPPickup` | Politique par défaut d’éligibilité au pickup et règles de pickup, appliquées par le domaine Pickup. |

Les fichiers de conteneur, de forme d’inventaire, de preset et d’acteur monde ont été déplacés de `PEPItemCore` vers `PEPInventory`. Le fragment `Equippable` et sa requête ont été déplacés vers `PEPEquipment`. Les réglages de pickup ont été déplacés vers `PEPPickup`. Les Core Redirects de `Plugins/PEPItems/Config/DefaultEngine.ini` conservent la résolution des types et propriétés réfléchis déplacés pour les assets existants.

`FPEPItemLocation` reste dans ItemCore comme contrat d’adresse de store. Le noyau conserve les formes génériques `ItemContainer` et `NamedSlot`; les règles qui donnent leur sens aux conteneurs et aux équipements restent dans les plugins de domaine.

## Résultats d’opération

`PEPItemCore` possède `FPEPItemResult` pour les règles et mutations liées à la validité d’un item, aux stacks et à son état. Les modules de domaine convertissent ce résultat à leur propre frontière publique : `FPEPInventoryResult`, `FPEPEquipmentResult`, `FPEPWeaponResult` et `FPEPInteractionResult`.

`FPEPItemResult` est le `FPEPOperationResult` de PEPCore augmenté de l’`ItemId` concerné : `bSuccess` et `ReasonTag` ont le même sens et utilisent les mêmes raisons canoniques `PEPCoreTags`. `ToOperationResult()` produit le résultat générique sans l’identité de l’item. L’ordre des arguments des fabriques reste asymétrique (`Success(ItemId, Reason)`, `Failure(Reason, ItemId)`) pour ne pas inverser silencieusement les nombreux appels existants.

## Requêtes, règles et mutations

- `PEPItemQuery::GetStackData`, `GetDurabilityData`, `GetResourceData` et `GetWeightData` partagent le même contrat : définition non chargée, état manquant ou valeur hors bornes donnent `false` et `bValid = false`, jamais une valeur devinée. `GetWeightData` exige donc une définition chargée (sinon l’absence de fragment `Weight` et une définition absente sont indiscernables) et, pour un item stackable, un état de stack valide.
- `PEPItemQuery::DoesItemMatchRequiredBlockedTags` est l’unique filtre de tags (slots de conteneur, slots d’équipement, clés) : item invalide ou définition non chargée refusés même avec des filtres vides; tous les tags requis doivent être possédés; aucun tag bloqué ne doit l’être. Les fonctions homonymes de `PEPItemRules` y délèguent.
- Les montants flottants non finis (NaN, infinis) sont refusés : réparation/dégâts de durabilité, écriture de valeurs scalaires flottantes. Les réglages de projet effectifs ramènent une durabilité de départ non finie à 1 et un poids non fini à 0.
- `PEPItemMutation::PreviewSplitStack` applique les mêmes règles et le même calcul que `SplitStack` sans allouer d’ID : l’item scindé porte `PEPItemMutation::GetPreviewItemId()` (`MAX_int32`), valeur que l’allocateur ne distribue jamais. Un enregistrement de prévisualisation ne doit être ni stocké, ni répliqué, ni sauvegardé.
- Un fournisseur de valeur scalaire qui annonce un tag sans valeur de définition ni valeur runtime est ignoré avant le contrôle de doublon : le résultat ne dépend plus de l’ordre des fragments.

## Accès aux stores

`FPEPItemAccessContext::IsSystem()` n’est vrai que pour un contexte construit par `FPEPItemAccessContext::System()`. Un contexte par défaut, `From(nullptr)` ou un contrôleur détruit en cours de requête ne sont pas le serveur. `IPEPItemStore::RegisterLocationValidator` n’a plus de valeur par défaut pour `bAllowAutoPlacement` : comme pour le contexte d’accès, un défaut sur une fonction virtuelle dépend du type statique de l’appel.

## Restriction Container / Resource

Une définition ne peut pas combiner les fragments `Container` et `Resource`. Dans la politique actuelle, épuiser une ressource retire l’item et tous ses descendants. Autoriser une ressource conteneur risquerait donc de détruire son contenu. La validation détaillée de l’inventaire refuse cette combinaison jusqu’à ce qu’un jeu définisse une politique de conservation, de vidage ou de suppression des descendants.

## Asset Manager

Le projet hôte doit scanner les types primaires `ItemDef` et `ItemPreset`. L’éditeur affiche un avertissement dans le Message Log PEP lorsqu’un scan manque et propose une action cliquable qui ajoute le scan correspondant à `Config/DefaultGame.ini`. Redémarrer l’éditeur après l’action afin que l’Asset Manager reconstruise son catalogue au démarrage.

Au démarrage de chaque `GameInstance`, `UPEPItemCoreSubsystem` précharge de façon asynchrone les définitions `ItemDef` configurées dans l’Asset Manager. `AreItemDefinitionsLoaded` n’est vrai que si le scan valide a découvert au moins une définition et que toutes sont chargées. Le gameplay qui dépend du catalogue complet doit récupérer ce subsystem, écouter `OnItemDefinitionPreloadFinished` (et relire l’état après abonnement), puis attendre `Ready` avant de s’activer.

États terminaux du préchargement : `Ready` signifie catalogue non vide et chargé; `Empty` signifie scan valide sans définition; `ConfigurationError` signifie scan ItemDef absent ou mal configuré; `Failed` signifie qu’au moins une définition découverte n’a pas pu être résolue; `Disabled` signifie que `bPreloadItemDefinitions` est désactivé. PEPInventory refuse l’ajout d’items et l’import d’arbres tant que le préchargement est `Loading`, vide, mal configuré ou en échec. En mode `Disabled`, il vérifie toujours chaque instance et exige que sa définition individuelle soit déjà chargée. Le subsystem ne suspend pas toute la partie : les autres systèmes qui dépendent du catalogue doivent appliquer eux-mêmes cette barrière.

Seules les définitions sont préchargées : leurs textures, meshes et autres références souples restent souples tant qu’un autre système ne les charge pas.

## Création d’instances et autorité

L’allocation d’ID n’est pas proposée comme nœud Blueprint autonome. Le point d’entrée Blueprint/C++ de création est `UPEPItemInstanceLibrary::CreateItemInstance(WorldContextObject, Definition, OutItem)` : il exige un contexte monde valide, refuse `NM_Client`, puis valide l’instance créée. Les presets d’items et de loadouts demandent également ce contexte pour produire des instances en jeu. La création de loadout depuis `UPEPInventoryComponent` est réservée à l’autorité.

La validation et l’aperçu des assets dans l’éditeur utilisent des fonctions C++ distinctes qui produisent des IDs transitoires. Elles ne sont pas exposées à Blueprint et ne doivent pas alimenter le gameplay. L’allocation basse couche et la duplication avec nouvel ID sont privées à l’instance; les helpers PEP internes et les tests sont les seuls accès directs. `RestoreFromSaveState` refuse aussi les appels sur un client. Les composants de domaine doivent continuer à vérifier l’autorité avant d’insérer ou de répliquer une instance.

Le compteur d’IDs est atomique : allocation et observation d’IDs chargés sont sûres depuis n’importe quel thread. Il ne recule jamais et ne distribue jamais `MAX_int32`, réservé aux prévisualisations. La migration unique de l’ancien slot de sauvegarde local s’exécute sur le game thread (au démarrage du subsystem). L’ancienne classe `UPEPItemIdAllocatorSaveGame` est désormais privée à `PEPItemCore`; son chemin réfléchi est inchangé.

Le registre de définitions transitoires des tests (`PEPItemAutomationTestSupport`) n’est consulté par `GetDefinitionIfLoaded` que pendant l’exécution d’un test d’automatisation (`GIsAutomationTesting`) : dans un build Development, serveur dédié compris, il ne peut pas masquer un asset réel.

## Sauvegarde

PEPItems et PEPInventory n’écrivent aucun fichier : le projet sérialise lui-même, avec son propre système de sauvegarde, le résultat de `UPEPInventoryComponent::ExportItemTrees` (un `FPEPInventoryTreePlacement[]`, un arbre par entrée racine avec sa `RootLocation`) et l’état de l’allocateur d’IDs, `UPEPIdAllocatorSubsystem::GetSaveState()`. Tout ce qui doit survivre à une sauvegarde porte le flag `SaveGame`, jusqu’aux champs des structs imbriquées, car une archive `ArIsSaveGame` ignore tout champ non marqué, même à l’intérieur d’une struct ou d’un `FInstancedStruct` :

- `FPEPItemInstance` (`SchemaVersion`, `ItemId`, `DefinitionId`, `StateFragments`) et `FPEPItemLocation` ;
- les états d’item (`FPEPItemState_Stack::Count`, `FPEPItemState_Durability::Durability`, `FPEPItemState_Resource::Current`, `FPEPItemState_WorldPresentation` et ses cosmétiques, `FPEPItemState_ContainerRuntime::bLocked` côté Inventory). Ces champs restent `EditAnywhere` pour que les presets décrivent un état initial, mais sont en lecture seule pour Blueprint : l’état change par le store propriétaire. Un stack à 0 est un état épuisé légal ;
- les enveloppes d’export Inventory (`FPEPInventoryTreePlacement`, `FPEPItemInstanceTree`, `FPEPItemInstanceTreeEntry`).

Un nouvel état d’item doit marquer `SaveGame` chacun de ses champs persistants. Les tests `PEP.Items.Save.*` et `PEP.Inventory.Save.SaveGameArchiveRoundTrips` font un vrai aller-retour par archive `ArIsSaveGame`.

`FPEPItemInstance` est versionné par `SchemaVersion`. La valeur par défaut est `UnversionedSchemaVersion` (0, « héritée / non versionnée ») : une archive sans ce champ — sauvegarde antérieure à sa persistance, ou champ omis par une sérialisation delta parce qu’il valait l’ancien défaut 1 — est donc reconnue comme ancienne au lieu d’être étiquetée courante, et tout enregistrement réel diffère du défaut et est toujours écrit. Les chemins de création (`MakeNew`/`InitializeNew`, donc `CreateItemInstance`, presets, `SplitStack`, tests) posent `CurrentSchemaVersion`; `Reset()` ramène l’enregistrement à l’état par défaut (0).

Au chargement depuis une archive, `PostSerialize` appelle `MigrateInstanceFromVersion` lorsque la version lue est plus ancienne que `CurrentSchemaVersion` : la version 0 est traitée comme des données de version 1 puis portée à la version courante. Une version plus récente que le build est laissée telle quelle et `ValidateInstance` la rejette; `ValidateInstance` rejette aussi un enregistrement resté à 0 (non migré). `PostSerialize` signale aussi chaque `ItemId` chargé à l’allocateur (`UPEPIdAllocatorSubsystem::ObserveLoadedId`), qui ne le redistribuera jamais, même si l’état de l’allocateur est restauré tard ou pas du tout. Pour passer à une version 2, incrémenter `CurrentSchemaVersion` et ajouter l’étape de migration dans `MigrateInstanceFromVersion`. `RepairLoadedInstance` reste un appel explicite du projet : il migre un enregistrement ancien et corrige les valeurs hors bornes (dans les mêmes bornes que `ValidateInstance`) au lieu de laisser `AddItemTrees` refuser l’arbre.

À la restauration, appeler d’abord `RestoreFromSaveState` sur l’allocateur (serveur uniquement; monotone, il ne fait jamais reculer le compteur sous les IDs déjà alloués ou chargés), puis `AddItemTrees(Placements)` sur l’inventaire vide, une fois les slots nommés enregistrés par les composants de domaine. L’export est l’inverse exact de `AddItemTrees` : ids, locations et état des items reviennent identiques. `PruneMissingDefinitions` (PEPLoadoutMutation) retire au préalable les arbres dont la définition a disparu.

## Vérification

Les tests d’automatisation ciblent notamment la validation et la réparation des ressources/durabilités, le refus des montants non finis, les opérations de stack avec un état invalide, la prévisualisation de split sans allocation, l’échec explicite des requêtes sans état ou sans définition, le contexte d’accès système, la migration des enregistrements non versionnés (`PEP.Items.Save.UnversionedRecordLoadsAsCurrent`), la non-réallocation des IDs chargés et le rejet d’enfants directs invalides dans un preset. Les tests Inventory appartiennent à `PEPInventory`; ceux des règles génériques restent dans `PEPItemCore`.
