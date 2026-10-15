# PEPCamera

Caméras réutilisables pour les projets PEP : première personne (FPS), troisième personne (TPS) et stratégie / vue de dessus (RTS, top-down), avec des transitions fondues entre modes.

Le plugin ne dépend que du moteur et d'Enhanced Input. Il ne touche ni à GMC ni à GMAS : la caméra est une présentation locale, elle ne réplique rien et ne modifie aucun état de mouvement, donc elle n'intervient jamais dans la prédiction ou le replay GMC.

## Principe

- `UPEPCameraComponent` (dérivé de `UCameraComponent`) se place sur l'acteur utilisé comme cible de vue, en général le pawn. À chaque mise à jour caméra, il évalue le mode courant et se déplace sur la vue obtenue.
- Un mode (`UPEPCameraMode`) calcule une vue (position, rotation, FOV) à partir d'un contexte : cible de vue, contrôleur local, position et rotation de la cible, rotation de contrôle.
- Le composant garde **une instance par classe de mode**, ce qui conserve l'état de chaque mode (zoom, point visé, épaule) d'une utilisation à l'autre. Pour régler un mode à l'exécution (FOV de visée, longueur de perche), passer par `GetCameraModeInstance`.
- Les modes passent par une pile de fondu (`UPEPCameraModeStack`) :
  - changer de mode fait un fondu de `BlendTime` secondes, avec une courbe `Linear`, `EaseIn`, `EaseOut` ou `EaseInOut` ;
  - revenir à un mode en cours de fondu sortant ne provoque pas de saut ;
  - un mode masqué par un mode entièrement fondu est retiré de la pile.

## Modes fournis

| Mode | Usage | Points clés |
| --- | --- | --- |
| `UPEPCameraMode_FirstPerson` | FPS | L'œil est un socket d'un mesh squelettique de la cible (`ViewSocketName`), sinon la position de vue du pawn (`BaseEyeHeight`). Il regarde selon la rotation de contrôle. `ViewOffset` s'exprime dans le repère du lacet (yaw) de contrôle. |
| `UPEPCameraMode_ThirdPerson` | TPS | Une perche derrière un pivot (acteur ou socket, plus `PivotOffset`), avec retard optionnel, zoom borné (`MinArmLength` / `MaxArmLength`) et décalage d'épaule permutable (`SwapShoulder`). Une sonde sphérique avance la caméra immédiatement devant un obstacle, puis la laisse revenir en douceur. |
| `UPEPCameraMode_Strategy` | RTS / top-down | Orbite autour d'un point visé au sol. Le tangage (pitch) passe de `PitchAtMinDistance` à `PitchAtMaxDistance` selon le zoom. Le zoom est proportionnel à la distance courante. En mode `Free` : déplacement du point visé selon le lacet de la caméra, défilement par bord d'écran optionnel, suivi du relief, limites (`FocusBounds`). En mode `FollowViewTarget` : le point visé suit la cible (jeu d'action en vue de dessus). |

Un projet ajoute ses propres modes en dérivant `UPEPCameraMode` (en C++ ou en Blueprint) et en surchargeant `ComputeView`. Les événements `On Activated` / `On Deactivated` servent par exemple à masquer la tête du personnage en première personne.

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
3. Afficher le curseur depuis le contrôleur si le défilement par bord d'écran est activé.

Le pawn se place sur le point visé : la pertinence réseau, l'audio et le streaming suivent ce que regarde le joueur. Le client propriétaire envoie ce point au serveur (RPC non fiable, au plus tous les `SyncInterval`, s'il a bougé de plus de `SyncDistance`). C'est une indication de pertinence réseau, jamais une autorité de jeu ; ne pas l'utiliser comme brouillard de guerre côté serveur.

**Vue de dessus qui suit un personnage**
Utiliser une sous-classe du mode stratégie avec `FocusMode = FollowViewTarget` sur le composant caméra du personnage.

## Limites actuelles

- Pas de panoramique par glisser-déposer de la souris : seules les entrées de vitesse et le bord d'écran déplacent le point visé.
- Pas de gestion de la réalité virtuelle (XR) ni de caméra orthographique dédiée : les réglages de projection du `UCameraComponent` sont recopiés tels quels.
- La sonde de collision TPS ignore uniquement la cible de vue. Les acteurs attachés (armes) doivent ignorer le canal `Camera`.

## Tests

`PEP.Camera.*` : fondu et retrait dans la pile, retour en cours de fondu sans saut, œil FPS, perche/épaule/zoom TPS, déplacement/zoom/tangage/suivi du mode stratégie, cycle de modes et routage des entrées par le composant. Ces tests tournent sans monde : les sondes de collision et de relief n'y sont pas exercées.
