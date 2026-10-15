# PEPCamera

Caméras réutilisables pour les projets PEP : première personne (FPS), troisième personne (TPS) et stratégie / vue de dessus (RTS, top-down), avec des transitions fondues entre modes.

Le plugin ne dépend que du moteur et d'Enhanced Input. Il ne touche ni à GMC ni à GMAS : la caméra est une présentation locale, elle ne réplique rien et ne modifie aucun état de mouvement, donc elle n'intervient jamais dans la prédiction ou le replay GMC.

## Principe

- `UPEPCameraComponent` (dérivé de `UCameraComponent`) se place sur l'acteur utilisé comme cible de vue, en général le pawn. À chaque mise à jour caméra, il évalue le mode courant, se place sur la vue obtenue, puis laisse `UCameraComponent::GetCameraView` construire la vue finale : projection, ratio, post-process, offsets additifs et XR restent gérés par le moteur. Hors monde de jeu (aperçu dans l'éditeur), il se comporte comme une caméra ordinaire et ne déplace pas son transform.
- Un mode (`UPEPCameraMode`) calcule une vue (position, rotation, FOV) à partir d'un contexte : cible de vue, contrôleur local, position et rotation de la cible, rotation de contrôle.
- Le composant garde **une instance par classe de mode**, ce qui conserve l'état de chaque mode (zoom, point visé, épaule) d'une utilisation à l'autre. Pour régler un mode à l'exécution (FOV de visée, longueur de perche), passer par `GetCameraModeInstance`.
- Les modes passent par une pile de fondu (`UPEPCameraModeStack`) :
  - changer de mode fait un fondu de `BlendTime` secondes, avec une courbe `Linear`, `EaseIn`, `EaseOut` ou `EaseInOut` ;
  - revenir à un mode en cours de fondu sortant ne provoque pas de saut ;
  - un mode masqué par un mode entièrement fondu est retiré de la pile ;
  - au retour vers un mode en cours de fondu, son poids de départ passe par l'inverse de sa courbe, ce qui garde l'image continue quelles que soient les courbes (exact avec deux modes dans la pile).
- La FOV produite est toujours bornée entre 5 et 170 degrés, même si un Blueprint écrit une valeur hors limites.
- `EndPlay` vide la pile : chaque mode reçoit `On Deactivated`.
- L'interpolation (retard du pivot, collision, zoom) est réinitialisée automatiquement après une téléportation de la cible (`ResetInterpolationTeleportDistance`) ou une absence d'évaluation (`ResetInterpolationAfterGap`, par exemple après une caméra de mort). `ResetCameraInterpolation` le fait à la demande.

## Modes fournis

| Mode | Usage | Points clés |
| --- | --- | --- |
| `UPEPCameraMode_FirstPerson` | FPS | L'œil est un socket d'un mesh squelettique de la cible (`ViewSocketName`), sinon la position de vue du pawn (`BaseEyeHeight`). Il regarde selon la rotation de contrôle. `ViewOffset` s'exprime dans le repère du lacet (yaw) de contrôle. |
| `UPEPCameraMode_ThirdPerson` | TPS | Une perche derrière un pivot (acteur ou socket mis en cache, plus `PivotOffset`), avec retard optionnel, zoom borné (`MinArmLength` / `MaxArmLength`) et décalage d'épaule permutable (`SwapShoulder`). Une sonde sphérique avance la caméra immédiatement devant un obstacle, puis la laisse revenir en douceur ; le retour est calculé en fraction de la perche, pour qu'un zoom arrière ne soit pas lissé deux fois. |
| `UPEPCameraMode_Strategy` | RTS / top-down | Orbite autour d'un point visé au sol. Le tangage (pitch) passe de `PitchAtMinDistance` à `PitchAtMaxDistance` selon le zoom. Le zoom est proportionnel à la distance courante. En mode `Free` : déplacement du point visé selon le lacet de la caméra, défilement par bord d'écran optionnel, suivi du relief (requête par type d'objet, `WorldStatic` par défaut, pour que les unités ne soulèvent pas le point visé). En mode `FollowViewTarget` : le point visé suit la cible (jeu d'action en vue de dessus). Les limites XY (`FocusBounds`, un `FBox2D`) s'appliquent dans tous les cas, y compris à `SetFocusLocation`. |

Un projet ajoute ses propres modes en dérivant `UPEPCameraMode` :
- en C++, surcharger le virtuel `ComputeView` (et `ResetInterpolation` si le mode lisse quelque chose) ;
- en Blueprint, implémenter l'événement `Compute View`. Il n'est appelé que s'il est implémenté, donc les modes natifs ne paient aucun appel Blueprint par frame. `Get Native View` renvoie la vue C++ du mode parent pour la retoucher.

Les événements `On Activated` / `On Deactivated` servent par exemple à masquer la tête du personnage en première personne. Les instances de mode persistent : une valeur modifiée à l'exécution (FOV de visée) reste jusqu'à ce qu'on la rétablisse ; les valeurs par défaut restent lisibles sur le CDO.

## Changer de mode

- `SetCameraMode(Classe)` demande un mode. Le fondu commence à la mise à jour caméra suivante. Une classe nulle revient à `DefaultCameraMode`.
- `CycleCameraModes` liste les modes parcourus par `CycleCameraMode` (par exemple bascule FPS ↔ TPS). Après le dernier mode, le cycle revient au premier.
- `DetermineCameraMode` est virtuelle : une sous-classe C++ peut choisir le mode d'après l'état de jeu (visée, véhicule…).
- `OnCameraModeChanged` est diffusé quand un nouveau mode commence son fondu.

## Entrées

- **La rotation (look) n'est pas une entrée caméra.** Elle reste sur le pawn (`AddControllerYawInput` / `AddControllerPitchInput`) et parvient aux modes FPS et TPS par la rotation de contrôle.
- Le composant transmet au mode courant :
  - `AddPanInput(X droite, Y avant)` : entrée de vitesse, multipliée par le delta time ;
  - `AddZoomInput` : positif pour zoomer, un cran de molette vaut 1 ;
  - `AddOrbitInput` : X fait tourner la caméra stratégie autour du point visé, en degrés × `OrbitSpeed`.
- Chaque mode interprète ces entrées à sa façon : le mode stratégie déplace son point visé et zoome, le mode TPS zoome sa perche, le mode FPS les ignore.
- `UPEPCameraInputConfig` (data asset) regroupe les actions `Pan`, `Zoom`, `Orbit`, `CycleMode` et `SwapShoulder`, toutes optionnelles. Appeler `BindCameraInput(EnhancedInputComponent, Config)` depuis `SetupPlayerInputComponent` du pawn ; un nouvel appel remplace les liaisons précédentes.
- Les mapping contexts restent à la charge du pawn ou du contrôleur. Avec PEPPlayer, les déclarer dans `GameplayMappingContexts` du contrôleur pour que le menu les suspende.

## Mise en place

**FPS / TPS sur un pawn existant**
1. Ajouter un `PEP Camera Component` au pawn.
2. Régler `DefaultCameraMode` (par exemple Third Person) et, si besoin, `CycleCameraModes` = [First Person, Third Person].
3. Créer des sous-classes Blueprint des modes pour régler leurs valeurs.
4. Lier l'entrée de cycle et le zoom via `BindCameraInput`.

Si le pawn a d'autres composants caméra, seul le premier actif est utilisé par le moteur.

**RTS**
1. Faire posséder au joueur un `APEPStrategyCameraPawn`, ou un Blueprint dérivé. Son composant caméra utilise le mode stratégie par défaut.
2. Renseigner `InputConfig` (Pan sur WASD avec un modificateur Swizzle pour l'axe Y, Zoom sur la molette, Orbit sur Q/E ou sur le déplacement de la souris avec bouton maintenu) et `InputMappingContext`.
3. Le défilement par bord d'écran ne tourne que si le contrôleur affiche le curseur (`ShouldShowMouseCursor`) et que le viewport a le focus. Avec PEPPlayer, activer `bShowMouseCursorInGameplay` sur le contrôleur. Pour le couper quand un menu est ouvert, surcharger `CanEdgeScroll` dans une sous-classe du mode.

Le pawn se place sur le point visé : ce qui lit la position du pawn (audio, streaming, pertinence quand elle se base sur le pawn) suit ce que regarde le joueur. Le pawn ne tick que sur la machine qui le contrôle localement.

Synchronisation avec le serveur :
- pendant le mouvement, le client envoie le point visé en RPC non fiable, au plus tous les `SyncInterval`, s'il a bougé de plus de `SyncDistance` ;
- une fois la caméra immobile depuis `SyncKeepAliveInterval`, il envoie une dernière fois la position exacte en RPC fiable, ce qui répare un paquet perdu ou un petit déplacement non transmis ;
- le serveur borne la position reçue aux `FocusBounds` du mode stratégie.

C'est une indication, jamais une autorité de jeu ; ne pas l'utiliser comme brouillard de guerre côté serveur.

**Vue de dessus qui suit un personnage**
Utiliser une sous-classe du mode stratégie avec `FocusMode = FollowViewTarget` sur le composant caméra du personnage.

## Limites actuelles

- Pas de panoramique par glisser-déposer de la souris : seules les entrées de vitesse et le bord d'écran déplacent le point visé.
- Pas de mode XR ni de mode orthographique dédiés : ces réglages viennent du `UCameraComponent` de base.
- La sonde de collision TPS ignore uniquement la cible de vue. Les acteurs attachés (armes) doivent ignorer le canal `Camera`.

## Tests

`PEP.Camera.*` : fondu et retrait dans la pile, retour en cours de fondu sans saut (linéaire et courbes différentes), inverse des courbes de fondu, FOV bornée, œil FPS, perche/épaule/zoom TPS, réinitialisation du retard TPS après téléportation, déplacement/zoom/tangage/suivi et limites du mode stratégie, cycle de modes et routage des entrées par le composant. Ces tests tournent sans monde : les sondes de collision et de relief n'y sont pas exercées.
