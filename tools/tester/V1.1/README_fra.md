<p align="center">
  <img src="/images/URTC_LOGO_TESTER.svg" alt="URTC Tester Logo" width="100%">
</p>

# URTC Tester (Windows / Linux)

**Version :** 1.1 · **Auteur :** JuanenRac (Electro Hobby 3D) &lt;electrohobby3d@gmail.com&gt;

Licence : **GPL-3.0**, la même que le firmware URTC et l'outil de
flashage - voir `LICENSE` à la racine du dépôt.

Un testeur de bus CAN en direct pour la carte URTC. Il se connecte via
le même adaptateur USB-CAN que celui utilisé par le flasher, demande à
la carte pour lequel de ses 12 profils d'outil elle est actuellement
configurée par cavaliers, et n'affiche que les contrôles et la
télémétrie propres à cet outil - pas une seule fenêtre essayant de
représenter les 12 à la fois. Tout ce qu'il fait est une commande
d'exécution ou une lecture de télémétrie contre l'application en cours
d'exécution ; il ne touche jamais la flash, donc il n'y a rien ici qui
puisse laisser la carte moins fonctionnelle qu'au départ.

## 1. Relation avec le flasher

Cet outil et `tools/flasher/V1.1/` partagent la même couche de transport
(les classes SLCAN et SocketCAN sont identiques) puisque les deux ont
finalement juste besoin de faire entrer et sortir des trames CAN du
même type d'adaptateur, mais ils font des travaux fondamentalement
différents :

| | Flasher | Tester |
|---|---|---|
| Touche la flash | Oui (c'est tout l'intérêt) | Jamais |
| Parle à | Le bootloader, principalement | L'application en cours d'exécution |
| But | Mettre à jour le firmware | Exercer/vérifier le matériel réel d'une tête d'outil |

Si vous n'êtes pas sûr de celui dont vous avez besoin : si la carte
exécute déjà le firmware et que vous voulez vérifier qu'un outil
fonctionne réellement (le chauffage chauffe, le moteur tourne, la LED
s'allume), vous voulez celui-ci.

## 2. Installation et exécution

Même schéma que le flasher :

```
cd tools/tester
pip install -r requirements.txt
python urtc_tester.py          # Windows
python3 urtc_tester.py         # Linux
```

Ou construisez un binaire autonome : `build_exe.bat` sous Windows,
`./build_exe.sh` sous Linux. Les deux nettoient d'abord `build/`/`dist/`
et empaquettent `assets/` (la bannière et l'icône) dans l'exécutable -
voir le propre README du flasher pour le raisonnement complet derrière
ces scripts, puisqu'il s'applique identiquement ici.

**Au démarrage**, la bannière s'affiche centrée à l'écran pendant 5
secondes avant que la fenêtre principale n'apparaisse, plutôt que de
vivre dans la fenêtre elle-même - comme le flasher, et pour la même
raison (garde la fenêtre elle-même compacte). L'icône de fenêtre/barre
des tâches est également un petit design autonome, pas la bannière
rétrécie.

### Barre de menu

- **Fichier** - Enregistrer les journaux (le journal à l'écran en texte
  brut ; pour un paquet plus complet incluant les diagnostics système,
  voir "Journaux et paquets de débogage" plus bas), et Quitter.
- **Langue** - basculer entre les 5 langues disponibles (voir "Langue"
  plus haut pour savoir comment fonctionnent les traductions).
- **Aide** - Lisez-moi (ouvre ce fichier dans une fenêtre visualiseur en
  lecture seule ; récupère automatiquement une version traduite dès
  qu'il en existe une pour la langue actuelle), GitHub d'URTC (ouvre le
  dépôt du projet dans votre navigateur), Licence (la licence GPL-3.0 de
  cet outil, lue depuis le fichier `LICENSE` du dépôt lui-même), et À
  propos (version et auteur).

### Structure des fichiers

Cet outil est organisé en modules par responsabilité, purement pour la
lisibilité - il n'y a aucune différence fonctionnelle entre les avoir
comme fichiers séparés ou comme un seul gros fichier. `tester_config.py`
contient les constantes de configuration/langue/protocole,
`tester_transports.py` contient SLCAN/SocketCAN, `tester_bus_monitor.py`
contient le thread de lecture CAN en arrière-plan, et `TesterGUI`
elle-même est divisée entre `tester_gui_core.py` (connexion, détection,
cycle de vie de la fenêtre, et la barre de menu) plus 3 mixins qu'elle
combine : `tester_common_panels.py` (panneaux contrôles globaux/F-RAM/
extension/auto-test/moniteur de bus/trame personnalisée),
`tester_panel_helpers.py` (utilitaires partagés utilisés par chaque
constructeur de panneau d'outil), et `tester_tool_panels.py` (les 8
constructeurs de panneaux spécifiques à un outil). `urtc_tester.py` est
maintenant juste le point d'entrée - démarrage sans CLI et l'écran de
démarrage.

**Langue** : anglais par défaut. Se change via le menu **Langue** (dans
la barre de menu en haut de la fenêtre) plutôt qu'une liste déroulante
dans la fenêtre principale - change l'interface (étiquettes, boutons,
dialogues, et messages de journal) vers l'une des 5 langues disponibles,
enregistre immédiatement dans `config.json` à côté de cet outil,
appliqué au prochain démarrage. Les traductions vivent dans des fichiers
texte brut sous `language/` (`english.lng`, `spanish.lng`, `italian.lng`,
`french.lng`, `german.lng`) sous forme de paires simples `CLE=Valeur`,
une par ligne - les lignes commençant par `#` et les lignes vides sont
ignorées, et un `\n` littéral dans une valeur devient un vrai saut de
ligne (utilisé par la poignée de messages de dialogue multi-lignes).
Modifiable directement si une traduction nécessite une correction, ou
comme point de départ pour une autre langue (ajoutez
`language/<nom>.lng`, ajoutez `("<nom>", "Nom Natif")` à
`AVAILABLE_LANGUAGES` près du début de `tester_config.py`, et définissez
`"language": "<nom>"` dans `config.json`). Une clé manquante d'un
fichier de langue retombe sur l'affichage du nom de cette clé même
plutôt que de planter, et un fichier de langue manquant ou illisible
(mauvaise modification, mauvais nom de fichier) retombe sur l'anglais
pour toute l'interface - dans les deux cas l'outil reste utilisable
pendant que le décalage se règle.

**Configuration SLCAN/SocketCAN sous Linux** (reflash de l'adaptateur,
permissions série, activation avec `ip link`) est exactement la même
que la section 1 du flasher - voir `tools/flasher/V1.1/README.md`
sections 1 et 2 plutôt que de la dupliquer ici.

## 3. Comment ça fonctionne

La fenêtre est disposée en 3 colonnes : gauche et centre contiennent les
sections toujours visibles ci-dessous (1-4, puis 6), droite contient le
panneau par outil de la section 5, qui est la seule partie de la
fenêtre qui change réellement selon ce qui est détecté. Diviser les
sections toujours visibles sur 2 colonnes plutôt que de les empiler
toutes en une seule empêche la fenêtre de devenir assez haute pour ne
plus tenir sur un écran ordinaire, à mesure que davantage de ces
sections ont été ajoutées au fil du temps. Le panneau propre de
l'imprimante 3D (le plus haut des 12) va un pas plus loin et divise ses
propres contrôles en 2 sous-colonnes en interne, pour la même raison.

**Connecter** (section 1, identique au flasher) : choisissez
Série/SLCAN ou SocketCAN, le port/interface, détectez éventuellement
automatiquement le débit binaire, puis Connecter.

**La détection se produit automatiquement à la connexion** (ou cliquez
sur **Détecter** pour la refaire) : l'outil envoie `0x110` (interroger
l'outil actif) et `0x7F8` (interroger la version), et utilise la
réponse pour :
- Montrer lequel des 12 profils d'outil est actif, et l'état général de
  la carte (toute erreur déclarée, défaut de bus CAN, encore dans
  l'écran de démarrage).
- Montrer le HardwareID et la version de firmware rapportés, signalant
  une discordance si cela ne correspond pas au propre
  `THIS_HARDWARE_ID` de ce projet.
- Construire le panneau **Contrôles d'Outil** à droite pour cet outil
  spécifique - et seulement cet outil. Changer quel outil est
  cavalié et détecter à nouveau démonte l'ancien panneau et en
  construit un nouveau depuis zéro.

**Contrôles Globaux** (section 2, toujours visible quel que soit l'outil
actif) : le remplacement de couleur de la LED de statut, la couleur et
l'allumage/extinction de l'anneau LED, et le mode d'affichage OLED
(`0x100`) - ceux-ci s'appliquent à chaque outil, donc ils ne se
déplacent pas vers le panneau dynamique. En mode Inspection AOI
spécifiquement, l'allumage/extinction de l'anneau ici est ignoré en
faveur du propre contrôle de stroboscope de cet outil (selon
`docs/CANBUS.TXT`) - la couleur s'applique quand même dans les deux cas.

**Carte d'Extension** (section 3, toujours visible) : le propre bus SPI
de `CONN_EXPANSION` et la ligne DIAG0 - rien d'autre ne vit sur ce
connecteur aujourd'hui -

**F-RAM de Persistance** (section 4, également toujours visible, mais
délibérément séparée de Carte d'Extension ci-dessus) : la FM24CL64B
partage le propre bus I2C2 matériel de l'OLED - un composant central de
la carte, pas quelque chose de câblé à `CONN_EXPANSION` du tout.
Regrouper les deux ensemble impliquerait une connexion entre eux qui
n'est pas réelle - le connecteur d'extension lui-même n'a pas de F-RAM,
pas d'EEPROM, rien de non volatile dessus.
- **Passage direct SPI** : tapez des octets hexadécimaux séparés par des
  espaces (1 à 7 d'entre eux, p. ex. `01 02 03`), appuyez sur Envoyer,
  et voyez exactement ce qui est revenu sur MISO pendant ce même
  transfert (`0x180`/`0x181`) - un transport d'octets brut, pas
  conscient des registres TMC5160, correspondant à la propre approche
  du firmware. Utile pour exercer le bus lui-même avant qu'il ne vaille
  la peine de construire un panneau dédié pour le protocole de
  registres d'une carte d'extension spécifique.
- **Niveau DIAG0** : **Interroger DIAG0** lit l'état actuel de la ligne
  de diagnostic de blocage/défaut d'un TMC5160 (`0x182`/`0x183`) - HIGH
  (inactif) ou LOW (affirmé). Une simple lecture interrogée, pas une
  valeur en direct/poussée - appuyez à nouveau sur le bouton pour
  l'actualiser.
- **F-RAM de Persistance** : **Interroger l'État** relit ce que la
  carte a sauvegardé la dernière fois avant une perte d'alimentation
  (`0x190`/`0x191`) - quel outil c'était, le point de consigne, si une
  erreur critique était active à ce moment-là. **Effacer la F-RAM...**
  l'efface (`0x192`, avec une boîte de dialogue de confirmation
  d'abord - ceci ne peut pas être annulé).
- **Type de carte d'extension** : **Interroger** montre laquelle des 5
  configurations possibles de `CONN_EXPANSION` est actuellement définie
  (`0x1A1` - voir `EXPANSION.TXT`). Lecture seule ici - définissez-la
  depuis la propre section CAN OTA de `URTC Flasher` à la place,
  puisque c'est une étape de configuration matérielle ponctuelle, pas
  quelque chose à changer avec désinvolture depuis un outil de
  diagnostic en direct.
- **Configuration libre de l'outil** : **Interroger** montre la lecture
  brute des cavaliers ID (0-31) à côté de ce que dit actuellement le
  registre `free_tool_selection` de la F-RAM (`0x1A3` - voir
  `EEPROM.TXT` section 5) - seulement réellement consulté par une carte
  dont les cavaliers lisent 0x1F/11111b. Lecture seule ici, même
  raisonnement que le type de carte d'extension ci-dessus - `URTC
  Flasher` est le seul outil qui l'écrit.
- **Type de périphérique et numéro de série** : **Interroger** montre le
  type de périphérique fixe (toujours URTC/0x03) à côté du numéro de
  série de l'appareil actuellement défini (`0x1A5` - voir `EEPROM.TXT`
  section 6), une étiquette attribuée par l'hôte pour distinguer
  plusieurs cartes par ailleurs identiques sur le même bus CAN. Lecture
  seule ici aussi - `URTC Flasher` écrit le numéro de série, cet outil
  ne fait que le relire.

**Trame CAN Personnalisée** (section 6, également toujours visible) :
une entrée d'ID brut + octets hexadécimaux avec envoi unique et
périodique - pour une commande qui n'a pas encore son propre contrôle
ici, ou pour tester quelque chose non (ou pas encore) documenté dans
`docs/CANBUS.TXT`. Aucune validation au-delà de la plage d'ID et
DLC≤8 ; quoi que ceci envoie est exactement ce qui va sur le bus. La
même section ouvre aussi le **Moniteur de Bus Brut** (voir ci-dessous).

**Exécuter Auto-test** (à côté de Détecter) : exécute un petit ensemble
de vérifications de communication sûres et au repos pour l'outil
actuellement détecté - confirme que l'interrogation de l'outil actif et
celle de la version répondent toutes les deux, puis (pour les outils
avec télémétrie) envoie un point de consigne/vitesse/puissance sûr de 0
et vérifie que la télémétrie attendue arrive. N'envoie délibérément
jamais rien qui chaufferait, tirerait, ou tournerait réellement à
puissance significative - ceci vérifie que l'aller-retour de
communication fonctionne, pas qu'un actionneur répond physiquement,
puisque confirmer cela nécessite de toute façon un humain qui observe.
Demande confirmation avant d'envoyer quoi que ce soit. Les outils sans
télémétrie (mouvement simple) ou qui sont purement pilotés par
événements (sonde de balayage) reçoivent une note informative
seulement au lieu d'un vrai succès/échec.

**Graphiques de température en direct** : les panneaux du fer à souder
et de la buse de l'imprimante 3D montrent tous deux un petit graphique
linéaire défilant à côté de leur lecture de température en direct - un
simple widget Canvas Tkinter, pas une nouvelle dépendance
(matplotlib/pyqtgraph casseraient la politique de zéro dépendance de
cet outil au-delà de pyserial). Échelle d'axe Y fixe (0 jusqu'au
plafond de point de consigne propre à cet outil) plutôt qu'auto-échelle,
donc la tendance est facile à lire d'un coup d'œil plutôt que l'échelle
se décale en dessous.

**Moniteur de Bus Brut** (ouvert depuis la section Trame CAN
Personnalisée) : une fenêtre séparée montrant chaque trame vue,
n'importe quel ID, indépendante du panneau d'outil actif - un tableau à
défilement en direct (Temps/ID/DLC/Données/Δt), Pause/Effacer, et une
lecture approximative de charge de bus/taux de trames (mise à jour une
fois par seconde ; le chiffre de charge ne modélise pas la surcharge de
bit-stuffing, donc traitez-le comme un chiffre diagnostique approximatif,
pas une mesure certifiée). **Exporter .trc...**/**Exporter .asc...**
enregistrent le tableau actuellement affiché comme un fichier de trace
simplifié au style PEAK PCAN-View / Vector CANalyzer respectivement -
assez proche pour être lisible par la plupart des outils qui attendent
ces formats, pas garanti identique octet par octet à ce que produisent
les applications réelles. Si `urtc_custom_ids.json` existe à côté de ce
script (optionnel, non inclus par défaut - `{"0x199": "My Sensor"}`),
la colonne ID montre ce nom convivial à côté de l'ID hexadécimal brut -
utile pour quiconque teste le propre trafic d'une carte d'extension
personnalisée sans avoir besoin de modifier le code source de cet
outil.

## 4. Couverture des outils

Chacun des 12 profils a son propre panneau, construit directement à
partir de `docs/CANBUS.TXT` :

| Outil | Contrôles | Télémétrie en direct |
|---|---|---|
| Fer à souder | Température de consigne, marche/arrêt | Température réelle, fin de course |
| Distributeur Pâte/Liquide, Tournevis, les deux Pinces | Direction + nombre de pas (mouvement unique) | aucune (0x120 partagé, aucune télémétrie pour aucun de ces 5) |
| Prélèvement par Aspiration | aucun | Lecture analogique, pièce détectée |
| Perceuse | Vitesse + direction | RPM réel, fin de course |
| Inspection AOI | Mode anneau (éteint/stroboscope/continu) + période de stroboscope | Fin de course |
| Graveur Laser | Puissance + armement/sécurité de l'interlock | Fin de course |
| Imprimante 3D | Consigne de buse, direction/pas de l'extrudeur, puissance ventilateur de couche, puissance ventilateur hotend | Température hotend, RPM ventilateur de couche, RPM ventilateur hotend |
| Sonde de Balayage | aucun | Nombre d'événements d'impact + horodatage (`0x095` priorité maximale) |

**Les watchdogs de communication sont gérés pour vous.** Le fer à
souder, le laser, et la buse de l'imprimante 3D ont chacun un watchdog
de 250ms dans le firmware ; le ventilateur de couche en a un de 1000ms.
Cocher la case "Actif" correspondante n'envoie pas seulement la
commande une fois - elle la renvoie automatiquement (150ms pour les
outils avec watchdog 250ms, 400ms pour le ventilateur de couche) tant
que la case reste cochée, de la même manière qu'un vrai contrôleur
maître doit le faire. La décocher envoie une seule trame zéro/arrêt et
s'arrête. Le ventilateur hotend n'a pas de watchdog (un détecteur de
blocage à la place - voir `docs/CANBUS.TXT`), donc c'est un simple
envoi unique.

## 5. Journaux et paquets de débogage

Comme le flasher : un journal de session horodaté est écrit
automatiquement dans `tools/tester/V1.1/logs/` (sûr à supprimer), et
**Exporter le Paquet de Débogage** enregistre un `.zip` avec le journal
actuel à l'écran plus des diagnostics système de base (OS, version
Python, transport/port/débit binaire actuel, outil détecté) pour
remettre à quiconque déboguant un problème de tête d'outil.

## 6. Limitations connues

- **Non testé contre du matériel réel.** Chaque pièce ici - la couche
  de transport, la gestion ID CAN/disposition d'octets, la
  synchronisation keepalive du watchdog - a été vérifiée isolément
  (trames simulées, un vrai sous-processus pour la synchronisation où
  pertinent) mais l'environnement qui a construit ceci n'a pas d'accès
  USB. Traitez une première session réelle avec la même prudence que
  demande le propre README du flasher.
- **Un panneau d'outil à la fois, par conception**, pas une limitation
  actuelle à supprimer plus tard - voir l'introduction ci-dessus pour
  la raison.
- **Les couleurs LED globales sont un remplacement direct**, pas une
  relecture en direct - il n'y a pas de télémétrie de ce que les LED
  de statut/anneau montrent réellement actuellement, seulement ce qui a
  été commandé en dernier.
