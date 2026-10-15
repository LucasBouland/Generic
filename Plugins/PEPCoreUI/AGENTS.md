# PEPCoreUI — guide pour les agents et LLM

## Mission et limites

`PEPCoreUI` est le socle d'interface partagé des plugins PEP. Il est construit sur **CommonUI** et fournit les classes de base, comportements et utilitaires UI qui doivent pouvoir être réutilisés par plusieurs jeux.

- Garder ici les abstractions UI véritablement communes : navigation activable, conventions de focus et d'entrée, contrôles ou helpers réutilisables, et contrats de style génériques.
- Garder l'interface d'un domaine dans son module `PEP<Plugin>UI` (par exemple `PEPItemUI` ou `PEPInventoryUI`). Ce module peut dépendre de `PEPCoreUI` et de son plugin métier.
- Garder le gameplay indépendant de l'UI : les modules métier ne doivent pas dépendre de `PEPCoreUI` simplement pour notifier un changement d'état.
- Ne pas lier les classes communes à un jeu, à un écran précis, à un niveau, à un PlayerController concret ou à un chemin d'asset propre à un projet hôte.
- Déclarer les dépendances de plugin dans le `.uplugin` et les dépendances de modules dans le `.Build.cs`. Si une API publique expose un type d'un module, ce module doit être une dépendance publique. Garder Slate en dépendance privée tant qu'aucun en-tête public ne l'expose.

## Choisir CommonUI, UMG ou Slate

### CommonUI — navigation et conventions transversales

Utiliser CommonUI pour composer les écrans multi-plateforme et router leur activation, focus et actions d'interface.

- Utiliser `UCommonActivatableWidget` pour une page, un menu ou une modale qui a un vrai cycle d'activation, une place dans l'arbre de routage, un focus à restaurer ou une action « retour ».
- Choisir un conteneur activable (stack, queue ou switcher CommonUI) quand il faut gérer une pile ou une navigation d'écrans. Ne pas rendre chaque widget enfant activable sans besoin de routage.
- Définir le focus attendu via les mécanismes CommonUI (`NativeGetDesiredFocusTarget` / `GetDesiredFocusTarget`) et la configuration d'entrée de l'écran (`GetDesiredInputConfig`) lorsque l'écran est actif.
- L'activation est distincte de `Construct`/`Destruct`. Un widget activable n'est pas auto-activé par défaut et ne reçoit pas automatiquement toutes les actions ; configurer explicitement l'activation, le focus, le retour et les actions nécessaires.
- Laisser CommonUI arbitrer les actions et la configuration d'entrée des widgets activables. Éviter que chaque écran bascule directement le mode d'entrée global ou manipule globalement Slate.
- Utiliser les éléments CommonInput/CommonUI pour que les prompts et interactions puissent suivre le type de périphérique lorsqu'ils sont concernés.

Pour un contrôle intégré qui n'est ni une page ni un nœud de navigation, utiliser une base de widget CommonUI ou UMG plus simple, telle que `UCommonUserWidget` ou un bouton CommonUI, selon le besoin.

### UMG — widgets UObject et composition d'écran

Utiliser UMG (`UUserWidget`, les widgets UMG et les assets Widget Blueprint) pour les écrans et contrôles qui bénéficient de la réflexion UObject, des propriétés éditables, des animations et de la composition visuelle dans l'éditeur.

- Utiliser `NativeOnInitialized` pour une initialisation liée à l'instance qui ne doit arriver qu'une fois.
- Utiliser `NativeConstruct` / `NativeDestruct` pour les abonnements et ressources qui suivent la présence du widget dans l'arbre Slate : ces callbacks peuvent se reproduire au cours de la vie d'une instance.
- Pour un `UCommonActivatableWidget`, gérer l'état actif avec `NativeOnActivated` / `NativeOnDeactivated`, sans confondre cet état avec la construction du widget.
- Déconnecter les delegates et libérer les ressources à la même frontière de cycle de vie où elles ont été acquises. Éviter les abonnements en double après une reconstruction.
- Garder les widgets réactifs à l'état du gameplay : ils en affichent les résultats et envoient des intentions/actions, sans devenir la source de vérité du domaine.
- Pour une classe C++ `UWidget` qui construit du Slate, créer la hiérarchie dans `RebuildWidget()` et libérer ses références Slate dans `ReleaseSlateResources()`.

### Slate — contrôles C++ bas niveau

Utiliser Slate directement pour un contrôle réutilisable qui exige un rendu, une disposition ou un comportement d'interaction précis, ou pour une hiérarchie que l'on ne souhaite pas exposer comme classe UObject/Blueprint.

- Construire avec les macros déclaratives Slate et les helpers adaptés (`SNew`, `SAssignNew`) ; garder les widgets Slate composables et concentrés sur l'affichage et l'interaction.
- Une instance Slate n'est pas un `UObject` : conserver les références Slate avec `TSharedRef` / `TSharedPtr` et utiliser `TWeakPtr` pour une référence non propriétaire quand c'est adapté.
- Pour un `UObject`, utiliser le modèle de propriété UObject (`UPROPERTY` avec `TObjectPtr`, ou une référence faible UObject quand elle est non propriétaire). Ne pas gérer un `UObject` avec les pointeurs partagés Slate.
- Ne pas conserver un `SWidget` au-delà de son propriétaire UMG ou du cycle de vie de ses ressources Slate. Nettoyer les références dans `ReleaseSlateResources()`.
- Éviter les captures brutes d'un `UObject` dans les callbacks différés ou lambdas. Utiliser un handle faible et vérifier sa validité avant d'agir.

## Disposition, DPI et rendu

- Le calcul de disposition commence par la taille désirée des enfants et remonte dans la hiérarchie ; l'arrangement distribue ensuite la géométrie parent vers enfants, puis le paint dessine le résultat. Choisir des panneaux/layouts adaptés plutôt que de calculer toute la géométrie à la main.
- Concevoir en unités Slate et avec le DPI scaling du projet. Ne pas appliquer une deuxième échelle DPI manuelle à des valeurs déjà fournies par le système.
- Prévoir les différentes résolutions, proportions, zones sûres, textes localisés et, si pertinent, le split screen.
- Utiliser la virtualisation pour les longues listes de lignes répétées quand le widget correspondant la prend en charge.
- `UWidgetComponent` sert aux interfaces dans le monde ; tenir compte du coût de ses render targets, de leur résolution et du nombre de composants actifs. Ne pas l'utiliser pour une interface écran classique.

## Données et performances

- Préférer les mises à jour par événements/delegates lorsque la donnée est coûteuse, irrégulière ou déjà notifiée par le gameplay.
- Une liaison UMG n'est pas interdite par principe : une lecture triviale peut être acceptable. Éviter les getters coûteux, les recherches ou allocations à chaque évaluation ; mesurer le coût dans le contexte réel.
- Ne pas ajouter de Tick à un widget pour une donnée qui change rarement. Pour les animations ou indicateurs réellement continus, choisir le mécanisme adapté et borner le travail effectué.
- Invalidation et caches évitent de recalculer inutilement layout/paint, mais ne sont pas gratuits ni une correction universelle. Invalider au bon niveau, préserver la lisibilité, puis profiler avant d'ajouter une `InvalidationBox` ou un `RetainerBox`.
- Un `RetainerBox` peut réduire le travail de dessin à certaines fréquences, mais introduit aussi rendu intermédiaire, latence ou coût GPU/mémoire. Le justifier par une mesure.

Le compendium joint affirme de façon absolue que les bindings de propriétés UMG ne devraient pas être utilisés. Appliquer plutôt la nuance de la documentation actuelle d'Epic : choisir entre polling et invalidation selon le coût et le rythme de la donnée, puis profiler ; une lecture simple n'a pas le même coût qu'une recherche ou un calcul exécuté à répétition.

## Focus, joueurs locaux et navigation

- Créer les widgets pour le bon joueur local et transmettre l'`Owning Player` lorsqu'il est disponible. Ne pas supposer que `PlayerController 0` correspond toujours au propriétaire du widget.
- Penser au focus par utilisateur Slate et aux contrôleurs par joueur en split screen. Vérifier la navigation clavier/manette, les limites de navigation et la restauration du focus.
- Garder l'input dans CommonUI et les systèmes d'input du projet. Le widget exprime sa configuration et ses actions ; éviter les modifications globales qui contournent le routeur CommonUI.
- Vérifier explicitement le comportement de « retour » des modales, pages empilées et overlays : le comportement attendu doit être configuré, pas présumé.

## Vérification et diagnostic

- Examiner le widget dans le **Widget Reflector** pour comprendre sa hiérarchie Slate, son focus et sa disposition.
- Utiliser Slate Insights / Unreal Insights et les outils Slate de la version du moteur pour mesurer les invalidations, le paint et les ticks avant d'optimiser.
- Vérifier aussi avec clavier/souris et manette, avec plusieurs résolutions/DPI et en split screen si l'interface est destinée à ces contextes.
- Les commandes Slate/debug peuvent changer entre versions : vérifier leur disponibilité dans l'UE ciblé avant de les documenter ou de s'appuyer dessus.

## Sources de référence

Le compendium fourni est une bonne introduction et une carte des sujets ; c'est un document communautaire et traduit, pas une référence normative ni un contrat d'API. Vérifier les signatures et comportements dans la version du moteur du projet (actuellement UE 5.8) et dans la documentation Epic.

- [UMG-Slate Compendium, dépôt communautaire](https://github.com/YawLighthouse/UMG-Slate-Compendium)
- [Common UI Plugin — documentation Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/common-ui-plugin-for-advanced-user-interfaces-in-unreal-engine)
- [`UCommonActivatableWidget` — API Unreal Engine](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/CommonUI/UCommonActivatableWidget)
- [Architecture Slate — documentation Unreal Engine](https://dev.epicgames.com/documentation/unreal-engine/understanding-the-slate-ui-architecture-in-unreal-engine?lang=en-US)
- [Invalidation Slate et UMG — documentation Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/invalidation-in-slate-and-umg-for-unreal-engine)
