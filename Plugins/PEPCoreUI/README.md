# PEPCoreUI — WBP communs (V1)

`PEPCoreUI` est le socle partagé des interfaces PEP, construit sur CommonUI. Les plugins de domaine gardent leurs écrans et leurs présentations propres dans un module `PEP<Plugin>UI`; ils peuvent utiliser les composants et styles communs de `PEPCoreUI`.

Ce document fixe le périmètre V1 et décrit les contrats effectivement livrés ainsi que les éléments reportés.

## État actuel du dépôt

- `PEPCoreUI` fournit le contrat C++ de cible visuelle (`UPEPDropTargetWidget`, `EPEPUIDropTargetVisualState`) et la configuration par monde (`APEPUIWorldSettings`, `FPEPUIDropTargetVisualStyle`).
- L'asset `/PEPCoreUI/UI/WBP_PEPDropTarget` est le WBP de référence. Il expose un `Border` nommé `DropTargetBorder`, utilise une texture blanche pour pouvoir être teinté, et ne capte pas les entrées souris.
- Aucun WBP métier Item/Inventory/Equipment/Interaction n'est fourni par ce V1. `PEPPlayerUI` possède `UPEPPlayerHUD`, basé sur `UCommonUserWidget`.
- `PEPInventoryUI` possède `UPEPInventoryWindow` et `UPEPInventorySessionLayer`, basés sur `UUserWidget`. Ce sont des widgets de domaine, pas des widgets CoreUI.

## Widgets communs à envisager

| Widget envisagé | Ce que fournit déjà Unreal/CommonUI | Recommandation pour PEP |
| --- | --- | --- |
| Bouton (`WBP_PEPButton`) | `UCommonButtonBase` gère l'interaction, la sélection et les états d'entrée; `UCommonButtonStyle` porte les états visuels. | Ne pas recopier le bouton. Créer un WBP commun seulement si PEP a besoin d'une composition ou d'un comportement partagé, par exemple icône + libellé + raccourci. |
| Panel/carte (`WBP_PEPPanel`) | `UBorder`, `UCommonBorder`, `Overlay`, `SizeBox`, `VerticalBox` et autres conteneurs UMG fournissent les briques de composition; `UCommonBorderStyle` porte le fond. | Pas de wrapper générique pour l'instant. Un style de bordure partagé suffit; les layouts de panneau restent dans les écrans qui en ont besoin. |
| Texte (`WBP_PEPText`) | `UCommonTextBlock` et `UCommonTextStyle` couvrent le texte réutilisable. | Aucun WBP de texte dédié. Affecter les styles communs aux widgets texte. |
| Onglet / navigation par onglets | `UCommonTabListWidgetBase`, `UCommonButtonBase` et `UCommonActivatableWidgetStack` couvrent la navigation et l'activation des écrans. | Pas de WBP d'onglet dans CoreUI; chaque écran configure ses entrées et son style. |
| Action clavier/manette | `UCommonActionWidget`, `UCommonBoundActionBar` et CommonInput affichent les actions et les icônes adaptées à l'entrée. | Pas de version PEP personnalisée, sauf présentation récurrente nécessitant un layout commun. |
| Garde de chargement | CommonUI fournit un load guard destiné à afficher un état de chargement tant que le contenu n'est pas prêt. | Utiliser le composant CommonUI plutôt que créer un doublon PEP. |
| Snackbar/toast (`WBP_PEPToast`) | CommonUI fournit les widgets de base et la gestion des écrans activables, mais pas un système générique de notifications transitoires de jeu avec durée, file et style PEP. | Bon candidat à un WBP CoreUI si plusieurs domaines en ont besoin. Définir le contrat de message et le présentateur séparément du visuel. |
| Dialogue modal (`WBP_PEPDialog`) | `UCommonActivatableWidget` et ses conteneurs gèrent l'activation, le retour et le routage de l'entrée; ils ne définissent pas le contenu ni les actions d'un dialogue. | Candidat conditionnel : créer un shell à slots (titre, contenu, boutons) seulement quand plusieurs plugins partagent le même contrat. Les confirmations métier restent dans leur domaine. |

## Périmètre WBP retenu pour la V1

- **Pas de bouton, texte, panel ou onglet PEP personnalisé par défaut.** Les widgets et styles CommonUI/UMG existants couvrent ces besoins.
- **Cible de drop générique (`WBP_PEPDropTarget`) :** composant visuel facultatif, sans règle d'acceptation. Il reçoit l'état calculé par le domaine cible et affiche les états neutre, survolé, valide ou invalide. Il ne transporte pas de contexte item/inventaire/équipement.
- **Coque de fenêtre flottante générique : différée.** À ce stade, seule la fenêtre de session d'inventaire est avérée; elle reste dans `PEPInventoryUI`. Ajouter une coque CoreUI quand un second domaine aura le même besoin de titre, déplacement, fermeture et slots de contenu.
- **Toast et dialogue génériques : candidats ultérieurs.** Les ajouter lorsqu'au moins deux domaines partagent leur contrat de présentation et de cycle de vie.

## Contrat livré : cible visuelle de drop

`WBP_PEPDropTarget` est une couche de rendu simple à superposer à une cible créée par un plugin de domaine. Le plugin destinataire garde la gestion des événements de drag/drop et décide si l'opération est permise. Il communique seulement l'état visuel à `UPEPDropTargetWidget` :

| État | Usage |
| --- | --- |
| `Neutral` | La cible n'est pas survolée. |
| `Hovered` | Un drag survole la cible, sans verdict affiché. |
| `Valid` | Le domaine a validé l'opération. |
| `Invalid` | Le domaine a refusé l'opération. |

Dans le WBP du domaine, ajouter `WBP_PEPDropTarget` comme enfant de l'overlay visuel, puis appeler `SetVisualState` lorsque le verdict change. Sa visibilité `HitTestInvisible` laisse le widget de domaine recevoir les événements. Le widget commun ne connaît aucun item, inventaire, slot ou payload, et ne déclenche aucune mutation.

Les couleurs sont lues dans le `WorldSettings` courant. `APEPUIWorldSettings` contient `DropTargetVisualStyle`, avec une couleur par état. Si le monde utilise encore `AWorldSettings`, la même structure sert de valeur de repli; les couleurs configurables par monde nécessitent la classe PEP ou une classe dérivée.

Pour activer cette configuration dans un projet hôte :

1. Si le projet n'a pas de classe `WorldSettings` personnalisée, choisir `APEPUIWorldSettings` dans **Project Settings > Engine > General Settings > World Settings Class**.
2. Si le projet a déjà sa propre classe, la faire dériver de `APEPUIWorldSettings`, puis sélectionner cette classe dérivée dans le même réglage. Ne pas remplacer une classe existante sans l'intégrer.
3. Régler `DropTargetVisualStyle` sur le World Settings de chaque monde concerné. La sélection de classe est au niveau projet; les valeurs de style sont sérialisées par monde.

Le plugin ne modifie pas automatiquement la classe `WorldSettings` du projet hôte. `WBP_PEPDropTarget` garde la couleur prévue par sa structure par défaut tant que le monde n'utilise pas `APEPUIWorldSettings` ou une classe dérivée.

Les widgets de domaine sont prévus dans leurs modules UI : `PEPItemUI` pour les entrées/infobulles d'items et le payload source, `PEPInventoryUI` pour la grille, les fenêtres de conteneur et leurs cibles, et un futur `PEPEquipmentUI` pour les cibles de slots d'équipement. Le domaine destinataire valide l'opération; CoreUI ne fournit que des briques visuelles sans sémantique métier.

## WBP fournis par les plugins de domaine

Oui, les plugins Item, Inventory, Equipment et Interaction peuvent livrer des WBP prêts à utiliser pour réduire le travail d'intégration. Ils doivent vivre dans leur module UI optionnel, pas dans `PEPCoreUI` ni dans le module gameplay : `PEPItemUI`, `PEPInventoryUI`, `PEPEquipmentUI`, `PEPInteractionUI`. Le plugin Unreal peut embarquer ses assets dans `Content/` avec `CanContainContent: true`; le projet hôte peut les utiliser tels quels ou les dériver.

Exemples de bons points de départ :

- **Item** : entrée visuelle (icône, nom, quantité, état) et tooltip. Garder les règles d'affichage génériques et alimentées par les données item.
- **Inventory** : une fenêtre d'inventaire grille/liste et un slot avec affordances de drag/drop. Les WBP restent une présentation de référence; le projet peut remplacer le layout sans remplacer le stockage ni les règles d'inventaire.
- **Equipment** : un slot d'équipement lié aux métadonnées de slots, plus éventuellement un panneau d'exemple. Éviter d'imposer un papier-poupée ou un arrangement de slots particulier.
- **Interaction** : un prompt contextuel écran qui compose le libellé d'action avec le widget d'action CommonUI pour afficher la touche/manette pertinente. Pour un prompt porté par un `WidgetComponent` dans le monde, préférer UMG standard : le routage CommonUI est conçu pour les interfaces écran et Epic déconseille son usage sur les widgets monde.

Ils ont surtout de la valeur s'ils rendent un plugin immédiatement visible et fonctionnel dans un projet vierge. Ils doivent rester optionnels et ne pas obliger le module gameplay à dépendre de UMG/CommonUI. Aucun WBP métier Item/Inventory/Equipment/Interaction n'existe actuellement dans le dépôt; le code de `PEPItemUI`, `PEPInventoryUI` et `PEPPlayerUI` existe, mais pas leurs assets WBP.

## Fenêtres et drag & drop : frontière de responsabilité

- `PEPItemUI` peut fournir la représentation visuelle de l'item et son payload générique de drag. Il ne décide pas si une destination accepte l'item.
- Le widget et le module UI de destination possèdent la cible et la règle de drop : grille/conteneur dans `PEPInventoryUI`, slot d'équipement dans un futur `PEPEquipmentUI`.
- `PEPCoreUI` peut fournir un feedback visuel de drop (survol, valide, invalide) sans connaître les items, stores, slots ou contextes. Ce composant n'a de valeur que s'il est réellement partagé.
- La fenêtre d'un conteneur d'inventaire reste dans `PEPInventoryUI`. Un shell flottant générique (barre de titre, déplacement, fermeture, slots de contenu) pourrait aller dans `PEPCoreUI` s'il est réutilisé par plusieurs domaines; il ne doit pas posséder le contexte ou la logique de la fenêtre.

## Réglages globaux via World Settings

Les couleurs d'une cible (neutre, survolé, valide et invalide) sont configurées au niveau du monde, pas codées en dur dans chaque WBP. Le domaine fournit seulement l'état logique; `WBP_PEPDropTarget` lit le style configuré dans le `WorldSettings` du monde courant et l'applique.

V1 fournit `APEPUIWorldSettings` dans `PEPCoreUI` avec `FPEPUIDropTargetVisualStyle`. Un projet peut dériver sa propre classe de cette base et régler sa classe dans **Project Settings > Engine > General Settings > World Settings Class**. Unreal sélectionne cette classe au niveau du projet, tandis que chaque monde possède son propre acteur `AWorldSettings` et ses valeurs sérialisées. Si le projet possède déjà une classe WorldSettings, elle doit intégrer cette base/ce contrat; PEPCoreUI ne doit pas remplacer cette classe silencieusement.

Les styles CommonUI de boutons/texte/bordures restent des styles de composants réutilisables. Le `WorldSettings` choisit les valeurs propres au monde pour l'apparence des cibles; les couleurs d'état ne sont pas dupliquées dans les WBP.

## Intérêt d'un outil de debug UI

Un overlay de debug est utile pour diagnostiquer les états et transitions que l'écran de jeu masque : item sélectionné/ID, conteneur et capacité, équipement associé, cible d'interaction et raison d'échec, écran activable au sommet, focus CommonUI et type d'entrée courant. Il peut aussi servir de banc de démonstration manuel pour les exemples WBP.

Garder l'interface de debug hors des écrans de production : prévoir un module Developer/Editor ou une couche opt-in de développement, désactivée en Shipping. Une séparation saine serait un shell d'overlay partagé si plusieurs domaines en ont besoin, tandis que chaque domaine UI fournit son propre panneau de données via un contrat de diagnostic en lecture seule. Le debug UI aide à observer et reproduire les problèmes; il ne remplace pas les tests automatisés.

## Gestion des styles avec CommonUI

CommonUI sépare le style du widget et son comportement. Il fournit notamment `UCommonButtonStyle`, `UCommonTextStyle` et `UCommonBorderStyle`; les widgets CommonUI référencent ces styles au lieu de recopier les propriétés visuelles dans chaque WBP.

1. Définir les styles partagés comme classes Blueprint de style CommonUI, par exemple bouton principal/secondaire, texte titre/corps et bordure de panneau.
2. Assigner explicitement le style au widget dans le WBP commun ou le WBP du domaine. Un même asset de style peut être réutilisé par plusieurs écrans.
3. Régler les `Template Styles` dans **Project Settings > Plugins > Common UI Editor**. Ils servent de style par défaut aux nouveaux widgets CommonUI et aux widgets auxquels aucun style explicite n'est affecté. Un style explicitement assigné dans un WBP garde la priorité.
4. Pour un jeu hôte ayant une identité visuelle différente, fournir ses propres classes de style et les affecter dans ses WBP dérivés/configurations. Garder dans `PEPCoreUI` des styles de base sobres et génériques.

CommonUI ne fonctionne pas comme une feuille CSS en cascade : une modification ne se propage qu'aux widgets qui référencent le style correspondant. Il n'est donc pas nécessaire de dériver chaque bouton ou panneau simplement pour changer ses couleurs ou sa typographie.

Le routage CommonUI est une configuration distincte des styles. Les projets qui utilisent des écrans activables, des actions « retour » ou des barres d'actions doivent aussi configurer leur `CommonGameViewportClient` et leur `CommonInputSettings.InputData`. `PEPCoreUI` ne choisit pas ces données d'entrée, car elles dépendent des actions et mappings du jeu. Le WBP `WBP_PEPDropTarget`, purement visuel, n'en dépend pas.

## Sources Epic

- [Common UI Quickstart Guide — widgets et styles](https://dev.epicgames.com/documentation/en-us/unreal-engine/common-ui-quickstart-guide-for-unreal-engine)
- [Common UI — plugin et bibliothèque de widgets](https://dev.epicgames.com/documentation/en-us/unreal-engine/common-ui-plugin-for-advanced-user-interfaces-in-unreal-engine)
- [UCommonButtonStyle](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/CommonUI/UCommonButtonStyle)
- [UCommonBorderStyle](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/CommonUI/UCommonBorderStyle)
- [UCommonTextStyle](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/CommonUI/UCommonTextStyle)
- [General Engine Settings — World Settings Class](https://dev.epicgames.com/documentation/en-us/unreal-engine/general-engine-settings-in-the-unreal-engine-project-settings)
