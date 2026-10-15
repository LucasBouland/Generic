# Instructions for coding agents

## Projet

- Lire [README.md](README.md) pour les objectifs et frontières des plugins réutilisables PEP.
- Garder le code indépendant d'un jeu hôte, sauf demande explicite.
- N'ajouter les dépendances plugin/module qu'aux endroits qui les utilisent directement.

## GMC et GMAS

Le kit de référence est dans [`AI/GMC-GMAS`](AI/GMC-GMAS/README.md). Il s'agit d'une analyse statique des archives fournies, pas d'une copie des sources ni d'une preuve de compilation ou de comportement runtime.

- Charger uniquement les notes pertinentes à la tâche. Pour le mouvement, consulter `docs/02-GMC-Core.md` et `docs/03-GMC-Prediction.md`; pour les capacités, effets et tâches GMAS, `docs/04-GMAS-Abilities.md`, `docs/05-GMAS-Effects-Attributes.md` et `docs/06-GMAS-Tasks.md`; pour les changements transversaux ou réseau, `docs/07-GMC-GMAS-Integration.md` et `docs/08-Multiplayer-and-Prediction.md`.
- Utiliser `indexes/PublicSymbols.md` pour trouver un symbole, puis vérifier sa signature et son comportement dans les en-têtes et implémentations des plugins réellement installés. Ces sources installées prévalent sur l'index et les notes.
- Le kit décrit un snapshot GMC 2.3.9 et GMAS dont le descripteur indique 1.4, avec certains changements documentés comme 1.4.1. Vérifier toute différence avec la version installée avant de coder.
- Lorsqu'une modification touche l'intégration mouvement/abilities, vérifier le transfert des cinq hooks GMC→GMAS : bind, pre-local move, prediction, ancillary et simulation.
- Ne pas déclencher d'effets irréversibles (dégâts, munitions, apparitions d'acteurs, etc.) dans un chemin de prediction/replay. Les valeurs persistantes lues pendant la prédiction doivent être bindées conformément au modèle GMC.
- Ne pas employer pour du nouveau code les API GMAS documentées comme dépréciées ou inertes dans ce snapshot : `SetAttributeValueByTag`, `GetQueuedAbilityCount` et `ExecuteSyncedEvent`. Vérifier aussi leur état dans la version installée.
- Garder l'autorité serveur pour les états de combat irréversibles et tenir compte des clients distants comme des listen servers lors de la validation réseau.
- Ne pas copier de code source GMC depuis le kit documentaire. Éviter de charger toutes les notes ou compétences GMAS en bloc ; consulter la compétence amont pertinente si elle existe dans le plugin installé.

L'outil de recherche hors ligne est disponible dans `AI/GMC-GMAS/tools/lookup.py`; il aide à trouver des symboles mais ne remplace ni l'inspection du code installé ni la compilation.
