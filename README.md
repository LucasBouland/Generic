# Generic — référentiel projet pour LLM

## But du projet

Ce projet Unreal Engine sert à concevoir et maintenir un ensemble de plugins réutilisables entre plusieurs jeux et projets. Le code placé dans les plugins PEP doit rester générique : il fournit des systèmes et des points d'extension, sans dépendre des règles, des assets ou des choix propres à un jeu donné.

Le projet `Generic` est l'environnement de développement de ces plugins. Il ne définit pas à lui seul le jeu qui les utilisera.

## Écosystème et dépendances

- **GMC**, **GMAS** et **UE5Coro** constituent les dépendances techniques communes qui seront utilisées dans l'écosystème. Un plugin PEP peut les déclarer comme dépendances lorsqu'il s'appuie directement sur leurs fonctionnalités.
- Les plugins **PEP** sont les plugins génériques appartenant à ce projet. Un plugin PEP peut dépendre d'un autre plugin PEP lorsque son domaine en a besoin ; ces dépendances doivent rester explicites et limitées aux besoins réels.
- **PEPCore** est le socle transversal des contrats réellement partagés : résultats d'opération génériques, type-valeur `FPEPItemId` stable, tags de raison canoniques, journalisation et réglages de debug. Il ne porte pas la création/persistance d'identifiants, les contrôleurs joueur, les adapters GMC/GMAS ni les bibliothèques utilitaires.
- **PEPCoreUI** est le socle transversal des interfaces PEP : widgets de base, comportements et helpers UI communs. Chaque plugin qui fournit sa propre interface garde un module dédié nommé selon le motif `PEP<Plugin>UI` (par exemple `PEPItemUI` ou `PEPInventoryUI`) et réutilise `PEPCoreUI`.
- **PEPPawn** possède les bases et composants de Pawn, y compris les adapters de mouvement/ability qui s'appuient directement sur GMC/GMAS.
- **PEPPlayer** possède les fonctions au niveau joueur et son contrôleur, indépendamment du Pawn. Son module `PEPPlayerUI` porte la présentation du joueur, dépend de `PEPCoreUI` et communique avec le contrôleur par un contrat afin que le module joueur ne dépende pas de l'implémentation UI.
- **PEPItems** possède les détails d'identifiant item : allocation, persistance, types de quantité et bibliothèque Blueprint. `FPEPItemId` reste défini dans PEPCore comme contrat sérialisé partagé par Items, Inventory, Equipment et Pickup.
- **PEPUtility** accueille les helpers réutilisables qui ne relèvent ni des fondations PEPCore, ni de l'UI, ni d'un domaine. Il ne doit pas devenir un fourre-tout ni dupliquer les responsabilités de PEPCore.

Quand une fonctionnalité est ajoutée, la placer dans le plugin qui possède son domaine. Garder les dépendances entre domaines explicites. Les modules `PEP<Plugin>UI` peuvent dépendre de leur plugin de domaine et de `PEPCoreUI` ; le code gameplay ne doit pas dépendre de l'UI sans nécessité. Éviter les références à un jeu, à un niveau ou à des assets propres à un projet utilisateur.

## Catalogue fonctionnel cible

Les onze plugins ci-dessous forment le catalogue PEP visé. `PEPCoreUI` fournit le socle UI commun ; les plugins de domaine peuvent fournir leur propre module UI au-dessus de ce socle. Les identifiants indiqués sont les noms de travail à utiliser pour les plugins sans identifiant déjà établi dans le dépôt.

| Domaine | Plugin | Responsabilité générale |
| --- | --- | --- |
| Pawn | `PEPPawn` | Bases et composants réutilisables pour les Pawns et personnages. |
| Player | `PEPPlayer` | Fonctionnalités au niveau du joueur, comme son contrôleur, son état et son cycle de vie, indépendamment du Pawn qu'il contrôle. |
| Item | `PEPItems` | Définitions et état des objets, ainsi que les concepts propres au domaine des items. C'est le nom déjà utilisé par le plugin existant. |
| Inventaire | `PEPInventory` | Possession, stockage, disposition et transferts d'items dans les inventaires. |
| Equipment | `PEPEquipment` | Équipement d'items et représentation de leur état équipé. |
| Interaction | `PEPInteraction` | Ciblage, validation et exécution d'interactions génériques. |
| UI partagée | `PEPCoreUI` | Briques d'interface réutilisables que les modules `PEP<Plugin>UI` partagent. |
| Utility | `PEPUtility` | Helpers et bibliothèques utiles qui ne sont pas à leur place dans PEPCore. |
| AI | `PEPAI` | Briques d'intelligence artificielle génériques et réutilisables. |
| Caméra | `PEPCamera` | Briques de caméra et comportements de caméra réutilisables. |
| Ballistique | `PEPBallistics` | Calculs et systèmes balistiques génériques. |

`PEPCore` reste le socle transversal général et ne fait pas partie de ces onze plugins. `PEPCoreUI` est le socle transversal réservé aux interfaces. Les identifiants proposés pour les nouveaux plugins sont des conventions de travail, à confirmer lors de leur création.

## Consignes pour les LLM

Avant de modifier ou d'ajouter du code :

1. Identifier le domaine propriétaire de la fonctionnalité et la placer dans son plugin PEP.
2. Vérifier si un contrat transversal existe déjà dans PEPCore avant d'en créer un autre. N'y ajouter ni comportement d'un domaine, ni écran/UI, ni simple helper.
3. Placer les briques UI communes dans PEPCoreUI et les interfaces propres à un domaine dans son module `PEP<Plugin>UI`.
4. Réserver PEPUtility aux helpers réellement génériques qui ne relèvent ni de PEPCore, ni de PEPCoreUI, ni d'un domaine fonctionnel.
5. Déclarer dans les fichiers de plugin et de module les dépendances effectivement utilisées, y compris GMC, GMAS ou UE5Coro lorsque nécessaire.
6. Préserver la réutilisabilité : ne pas introduire de règles de jeu, de chemins d'assets ou de dépendances implicites propres au projet hôte.

La présence d'un plugin dans le dépôt ne suffit pas à en faire un domaine du catalogue cible. Ce README ne prescrit pas la migration ou la suppression des plugins existants qui n'y figurent pas.
