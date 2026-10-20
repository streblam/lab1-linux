# Laboratorijas darbs Nr. 1 — procesu pārvaldnieks Linux vidē

C++17 programma ar īstiem Linux procesiem, divvirzienu AF_UNIX IPC, prioritāšu vadību, INI konfigurāciju un žurnālu. Programmai nav ārēju bibliotēku atkarību. Python 3 vajadzīgs tikai automatizētajiem testiem.

## Ātrā palaišana Linux vidē

Atver termināli tieši šajā projekta mapē:

```bash
make
./oslab --init
./oslab
```

`--init` vienreiz izveido `$HOME/.config/tsi-os-lab1/config.ini`. Failu apstrādes ceļš tiek piesaistīts pašreizējās projekta mapes `data` apakšmapei. Esoša konfigurācija netiek pārrakstīta. Ja pārvieto projektu, izlabo konfigurācijas `directory` ceļu.

Ja Ubuntu/Debian vidē nav kompilatora:

```bash
sudo apt update
sudo apt install build-essential python3
```

Pašu programmu palaid **bez sudo**. Tas ļauj demonstrēt normāla lietotāja prioritāšu ierobežojumus. Atbalsta mērķis: Linux ar `/proc`, GCC 9+ vai atbilstošu Clang, C++17, GNU Make. Testēts pievienotajā `docs/TESTESANA.md` norādītajā vidē. macOS nav šīs programmas izpildes platforma; HTML atskaiti gan var atvērt Mac pārlūkā.

## Aizstāvēšanas režīms

```bash
./oslab --interactive --delay-ms 1500
```

Fails tiek apstrādāts pēc 1,5 sekunžu demonstrācijas aiztures. Reālā failu apstrāde lieto `open/read/fstat/close`, nevis mākslīgus rezultātus. Pēc visu darbu pabeigšanas procesi paliek dzīvi, līdz ievadi `shutdown`.

Supervisor terminālī ieraksti:

```text
status
priority 1 LOW
priority 1 HIGH
priority 1 NORMAL
shutdown
```

`LOW` → nice 10, `NORMAL` → nice 0, `HIGH` → nice −5. Priority komandu izpilda attiecīgais Worker pēc iepriekšējās komandas pabeigšanas. Tātad aizņemtam Worker tā var stāties spēkā pēc faila apstrādes, bet `IDLE` stāvoklī tā tiek izpildīta uzreiz.

`LOW` parasti izdodas; atgriešanās uz `NORMAL` un pāreja uz `HIGH` parastam lietotājam parasti tiek atteikta. Precīzi izšķir OS atļaujas, CAP_SYS_NICE un RLIMIT_NICE. Prioritātes atteikums tiek ierakstīts žurnālā un neaptur programmu.

**Worker pārtraukšana:** no `START worker=... pid=...` paņem PID. Otrā terminālī aizstāj `12345` ar reālo Worker PID:

```bash
kill -KILL 12345
```

Nenogalini Supervisor. Žurnālā parādās `UNEXPECTED_EXIT ... signal=9`; ja procesam bija nepabeigts uzdevums, tas tiek nodots citam dzīvajam Worker (`REQUEUE`). Pārējie Worker turpina darbu. Ja nepieciešams vairāk laika, izmanto `--delay-ms 5000` vai palielini failu skaitu.

Pilna demonstrācija un skaidrojumi: `docs/AIZSTAVESANA.md`.

## Konfigurācija

Atrašanās vieta vienmēr ir `$HOME/.config/tsi-os-lab1/config.ini`; XDG_CONFIG_HOME šajā darbā netiek lietots. Piemērs (`/home/student` aizstāj ar saviem ceļiem):

```ini
[supervisor]
workers=3
directory=/home/student/os-lab1-linux/data
log=/home/student/.config/tsi-os-lab1/supervisor.log

[priorities]
worker1=NORMAL
worker2=NORMAL
worker3=NORMAL
```

Atļauti 3–5 Worker. Ja norādi 4 vai 5, obligāti pievieno atbilstošo `worker4`/`worker5` prioritāti. Ja samazini skaitu, liekās prioritāšu atslēgas noņem. Nezināmas, trūkstošas un dublētas atslēgas tiek noraidītas. INI ceļiem jābūt absolūtiem; nelieto pēdiņas vai `~`. Atstarpes ceļa vidū ir atļautas. INI neatbalsta vairāku rindu vērtības, un vērtību sākuma/beigu atstarpes tiek nogrieztas. Komentāri ir atsevišķās rindās ar `#` vai `;`.

Konfigurācija tiek lasīta nākamajā startā. Apstrāde ir tikai norādītās mapes pirmajā līmenī; apakšmapes, simboliskās saites un FIFO tiek izlaisti. Žurnāla fails (arī tā hardlink) netiek iekļauts apstrādes sarakstā.

## Rezultāti un izejas kodi

- `RESULT bytes lines checksum`: nolasīto baitu skaits, rindu skaits, FNV-1a 32 bitu kontrolsumma decimāli.
- Tukšam failam ir 0 rindas. Pēdējā netukšā rinda skaitās arī bez `\n`. Bināram failam “rindas” nozīmē to pašu baitu `\n` skaitīšanas noteikumu.
- Kontrolsumma nav kriptogrāfiska. Mainot failu apstrādes laikā, rezultāts raksturo faktiskajā lasīšanā saņemtos baitus; faila momentuzņēmums netiek veidots.
- `0`: normāla pabeigšana; arī korekti apstrādāts prioritātes atteikums.
- `1`: konfigurācijas, palaišanas vai cita fatāla kļūda.
- `2`: Worker zudums, faila kļūda, termiņa pārsniegums vai pārtraukta partija. Arī sekmīgi pārplānots darbs neatceļ faktu, ka Worker avarēja.

Žurnāls tiek papildināts, nevis dzēsts. `path_hex` un kļūdas teksts IPC ir UTF-8/baitu virknes heksadecimālais pieraksts: tas droši atbalsta atstarpes, tabulācijas un jaunrindas failu nosaukumos, nebojājot protokolu vai loga rindas. Failu vārdi patiesībā tiek apstrādāti kā Linux baiti; nav nepieciešams, lai tie būtu UTF-8.

## Pabeigšana un termiņi

Normālā režīmā pēc darbu beigām automātiski nosūta `SHUTDOWN`. Interaktīvajā režīmā to pieprasa ar `shutdown`, Ctrl+D (stdin EOF) vai Ctrl+C. SIGTERM arī uzsāk sakopšanu. SIGINT/SIGTERM rezultāts ir kods 2.

Pēc pabeigšanas pieprasījuma jauni uzdevumi vairs netiek sadalīti. Worker mēģina pabeigt jau pieņemto uzdevumu un atbildēt `STOPPED`. Ja 3 sekundēs nebeidzas, Supervisor izmanto `SIGKILL` un `waitpid`. Tāpēc agrīna `shutdown` komanda var atstāt neapstrādātus failus; to norāda `pending` un kods 2.

Starta/aktīva darba noklusētais termiņš ir 60 sekundes. Lieliem vai lēni lasāmiem failiem to palielini, piemēram:

```bash
./oslab --timeout-seconds 300
```

Dīkstāves Worker interaktīvajā režīmā netiek nogalināti. Ja visi Worker pazūd un paliek darbi, programma pabeidzas ar kļūdu, nevis gaida bezgalīgi. Darbs var tikt atkārtots uz atlikušajiem Worker; jauni Worker avārijas vietā netiek palaisti. Nekontrolējams Supervisor SIGKILL vai strāvas zudums nav korektas SHUTDOWN demonstrācija.

## Testi

```bash
make test
```

12 integrācijas testi izmanto pagaidu HOME un konfigurāciju; tava parastā konfigurācija netiek mainīta. Testos Python palaiž/novēro izpildāmo failu; **visas programmas procesu, IPC, prioritāšu un konfigurācijas darbības ir C++ zemā līmeņa Linux API kodā**. Testu palaišanas rezultāti aizstāj `docs/test-evidence` attiecīgā nosaukuma logus.

Papildu pārbaude ar UndefinedBehaviorSanitizer:

```bash
make debug
OSLAB_BINARY="$PWD/oslab-ubsan" OSLAB_EVIDENCE="$PWD/docs/ubsan-evidence" python3 tests/integration.py
```

## Faili

| Fails | Nozīme |
|---|---|
| `src/main.cpp` | CLI, režīma izvēle, kļūdu augšējā robeža |
| `src/config.cpp` | INI izveide, lasīšana un validācija ar OS API |
| `src/common.hpp` | FD RAII, IPC ierāmēšana, kodēšana, kopējie tipi |
| `src/worker.cpp` | Komandas, failu apstrāde, prioritātes |
| `src/supervisor.cpp` | fork/exec, poll, sadale, uzraudzība un žurnāls |
| `tests/integration.py` | Automatizētie integrācijas testi |
| `data/` | Pieci demonstrācijas faili |
| `docs/ATSKAITE.md`, `ATSKAITE.html` | Atskaites saturs; HTML var atvērt pārlūkā un izdrukāt |
| `docs/AIZSTAVESANA.md` | Secīga demonstrācija un skaidrojamie jautājumi |
| `docs/TESTESANA.md` | Faktiskās pārbaudes, vide un robežas |

ZIP ietver pirmkodu, nevis vienai arhitektūrai piesaistītu Linux bināru. `make` izveido programmu mērķa Linux datorā. Pirms iesniegšanas papildini atskaites autora/grupas laukus un pielāgo arhīva nosaukumu kursa vispārīgajām iesniegšanas prasībām, kuras šeit nebija pievienotas.
