# Faktiskās testēšanas rezultāti

Pārbaudes izpildītas 2026-10-19 pieejamajā Linux izpildvidē. Šie ir izpildes rezultāti, nevis tikai piedāvāts testēšanas plāns.

## Vide

- Linux kodols 6.18.55, x86_64.
- GCC g++ 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04).
- C++17; `-O2 -Wall -Wextra -Wpedantic -Werror`.
- Atsevišķs build ar `-fsanitize=undefined`, atkārtojot visu testu kopu.
- UID 0 izolētā lietotāju/PID vidē; CapEff = 0. Hosta administratīvās tiesības nav pieejamas. Prioritātes paaugstināšana tiešām tika atteikta.
- Logu laika josla ir testēšanas procesa vietējā josla (+0200), nevis obligāti lietotāja datora laika josla.

## Rezultāti

| Tests | Ko pārbauda | Rezultāts |
|---|---|---|
| normal-3-workers | 3 īsti Worker; baiti, rindas un FNV-1a pret neatkarīgu referenci | PASS |
| normal-5-workers | 5 Worker un konfigurācijas skaita ievērošana | PASS |
| priority-permissions-fd-count | LOW izdodas, HIGH un NORMAL atteikti; FD skaits stabils | PASS |
| worker-sigkill-requeue | SIGKILL aizņemtam Worker; darbs pārplānots, visi 12 faili pabeigti | PASS |
| file-removed | Fails izdzēsts pēc PROCESS; saņemts ERROR PROCESS ENOENT | PASS |
| supervisor-sigterm | Supervisor SIGTERM uzsāk kontrolētu pabeigšanu | PASS |
| shutdown-timeout | Worker apturēts ar SIGSTOP; pēc 3 s nogalina un sagaida | PASS |
| invalid-config | workers=2 tiek noraidīts pirms Worker palaišanas | PASS |
| init-no-overwrite | INI izveide un esošā faila aizsardzība pret pārrakstīšanu | PASS |
| all-workers-dead | Visi Worker nogalināti; Supervisor pabeidzas bez iestrēgšanas | PASS |
| empty-directory | Tukša failu partija un normāls visu procesu shutdown | PASS |
| task-timeout | Aktīva darba termiņš; pārplānošana un visu Worker zuduma apstrāde | PASS |

Normālajos failu testos pārbaudīts tukšs fails, fails bez beigu newline, binārs fails (1 280 000 baitu), nosaukumi ar atstarpēm, Unicode, tabulāciju, newline un pēdiņu. Direktorijs, simboliska saite un FIFO tika izlaisti.

Kopsavilkums: **12/12 PASS normālajā build; 12/12 PASS UBSan build**. UBSan diagnostika netika konstatēta. Kompilācija pabeigta bez brīdinājumiem ar `-Werror`.

## Resursu pārbaude

Interaktīvajā dīkstāvē katram Worker ir četri FD: stdin, stdout, stderr un paša IPC. Supervisor ir septiņi FD: 0–2, žurnāls un trīs IPC galapunkti. Prioritāšu un statusa komandas šo skaitu nemaina. Pēc testa pabeigšanas pārbauda, ka bērnprocesu `/proc` ieraksti vairs nav palikuši; programma visus bērnus sagaida ar waitpid.

Testu harness ņem vērā konteinerus, kur `/proc` var būt piesaistīts augstākai PID namespace: pārbaude meklē NSpid pēdējo numuru. Tā ir testa novērošanas īpatnība, nevis prasība parastam Linux datoram.

## Pierādījumi

- `test-output.txt`: gala normālās testu izpildes izvade.
- `ubsan-test-output.txt`: sanitizatora testu izvade.
- `test-evidence/*.log`: katra normālā testa Supervisor žurnāls un, ja vajag, stderr.
- `ubsan-evidence/*.log`: atbilstošie sanitizatora žurnāli.

Piemēram, prioritātes testā novērots `PRIORITY LOW 10`, pēc tam `ERROR PRIORITY 13 ...`. Worker avārijas testā novērots `UNEXPECTED_EXIT ... signal=9`, `REQUEUE`, pēc tam `SUMMARY completed=12 failed=0 pending=0 children_alive=0 exit_code=2`. Kods 2 pareizi saglabā avārijas faktu.

## Pārbaudītās robežas

Nav apgalvots, ka programma izpildīta visās Linux distribūcijās vai ARM64. Nav testēta īsta diska kļūme, OOM, strāvas pazušana, neatceļama kodola I/O gaidīšana vai uzbrukums no kompromitēta Worker. Tā ir lokāla laboratorijas sistēmprogramma. Linux datorā ar C++17 kompilatoru pirmkods ir jākompilē no jauna. Priviliģētā vidē prioritātes tests var izmantot nobody kontu; izolētā vidē bez CAP_SYS_NICE tiesību atteikumu demonstrē esošais konts.

Vispārīgās kursa iesniegšanas prasības nebija iesniegtas. ZIP struktūra ir projekta mape ar pirmkodu, atskaiti, testiem un instrukcijām; nepieciešamais arhīva nosaukums un autora/grupas dati jāpielāgo pirms iesniegšanas.
