# Aizstāvēšanas scenārijs

## 1. Parādi īstus procesus

Projekta mapē:

```bash
make
./oslab --init
./oslab --interactive --delay-ms 1500
```

Ja konfigurācija jau eksistē, `--init` izlaiž; tā atteikums pārrakstīt esošo failu ir paredzēts. Pirmajā terminālī ievadi `status`. Otrajā terminālī:

```bash
ps -eo pid,ppid,ni,stat,args
```

Atrodi Supervisor un trīs `oslab --worker` procesus. Katram ir savs PID. Worker PPID ir Supervisor PID. Starta logā redzams identifikators, PID un laiks. `ps` un `kill` ir demonstrācijas instrumenti, nevis programmas implementācija.

## 2. Parādi rezultātus un divvirzienu IPC

Logā atrodi `COMMAND ... PROCESS`, `RECV ... READY` un `RECV ... RESULT`. Izskaidro, ka viens AF_UNIX SOCK_SEQPACKET pāris katram Worker nodod ziņojumus abos virzienos. ID un FD numurs tiek nodots `exec` argumentos.

Demonstrācijas failam `data/04-skaitli.txt` ir 10 baiti un 1 rinda. Tukšajam failam ir 0 baiti un 0 rindas. Pārējo rezultātu references aprēķina testi neatkarīgi Python valodā.

## 3. Prioritātes eksperiments

Pēc `IDLE` pirmajā terminālī:

```text
priority 1 LOW
priority 1 HIGH
priority 1 NORMAL
```

LOW sekmes pierāda `PRIORITY LOW 10`. Parastam lietotājam HIGH un arī atgriešanās uz NORMAL pēc LOW parasti beidzas ar `ERROR PRIORITY`. Žurnālā ir OS kļūdas numurs; programmas izpilde turpinās. Ja kontam ir īpašas tiesības, paaugstināšana var izdoties — tā nav kļūda programmā. Aizstāvēšanā lieto parastu kontu bez sudo.

`setpriority(PRIO_PROCESS, 0, value)` izsauc pats Worker pēc Supervisor komandas. 0 apzīmē pašu procesu. Tā kā Worker ir viens pavediens, nav Linux vairāku pavedienu nice atšķirību. `getpriority` pārbauda rezultātu; errno pirms tā jāiestata 0, jo negatīvs rezultāts pats par sevi vēl nav kļūda.

## 4. Piespiedu Worker pārtraukšana

Pārtrauc vienu Worker otrā terminālī ar `kill -KILL PID`, kur PID paņem no pašreizējās palaišanas loga. Interaktīvais režīms procesus patur arī pēc failu pabeigšanas, tāpēc šo demonstrāciju var veikt jebkurā brīdī.

Supervisor parāda `UNEXPECTED_EXIT ... signal=9`; pārējie paliek dzīvi, `status` darbojas. Ja vēlies parādīt arī darba atgūšanu, restartē ar `--delay-ms 5000` un nogalini Worker pēc `PROCESS`, pirms tā `RESULT`. Tad redzēsi `REQUEUE` un rezultātu no cita Worker. Automātiskajā testā tas ir pārbaudīts ar 12 failiem.

## 5. Konfigurācijas izmaiņas

Pabeidz programmu ar `shutdown`. Atver `$HOME/.config/tsi-os-lab1/config.ini`. Nomaini `workers=3` uz `workers=4` un pievieno `worker4=LOW` sadaļā `[priorities]`. Nākamajā palaišanā parādās četri PID un ceturtā Worker prioritātes komanda. Mainot atpakaļ uz 3, noņem `worker4`.

Paskaidro, ka INI saturs ir nolasīts ar `open/read/close`, bet sākotnēji ierakstīts ar `open/write/fsync/close`. C++ virkņu un map lietošana teksta parsēšanai nenoslēpj prasītās OS darbības.

## 6. Korekta resursu atbrīvošana

Lai parādītu pilnīgi normālu pabeigšanos, veic jaunu palaišanu bez Worker nogalināšanas. Pēc `IDLE` ieraksti `shutdown`. Redzēsi katram Worker `STOPPED`, pēc tam `EXIT ... exit=0` un `SUMMARY ... children_alive=0 exit_code=0`.

Saglabā startā parādītos PID. Pēc programmas beigām otrajā terminālī pārbaudi tos, piemēram (aizstāj skaitļus):

```bash
ps -p 12345,12346,12347 -o pid,ppid,stat,args
```

Worker rindām vairs nav jābūt; arī zombie netiek atstāti, jo Supervisor lieto `waitpid`. Kamēr Worker ir dzīvs, `ls -l /proc/PID/fd` standarta palaišanā parāda 0, 1, 2 un vienu IPC FD. Faila lasīšanas laikā īslaicīgi ir arī apstrādājamā faila FD. Ligzdām nav failu sistēmas nosaukumu, tāpēc pēc tām nav jādzēš socket faili. Vecus PID nevajag atkārtoti pārbaudīt ilgi pēc darba beigām, jo Linux tos var izmantot citam procesam.

## Jautājumi, kuri jāsaprot

- **Kāpēc fork un exec abi?** fork izveido bērnprocesu, exec aizvieto tā programmas attēlu un ieiet Worker režīmā.
- **Kāpēc socketpair?** Abi gali ir divvirzienu; SOCK_SEQPACKET saglabā ziņojumu robežas. Nav jāveido divas pipe plūsmas un to ierāmēšana.
- **Kā Supervisor neiestrēgst uz viena Worker?** Parent galapunkti ir nonblocking, `poll` paziņo par pieejamiem datiem, vienam Worker vienlaikus piešķir vienu failu.
- **Kāpēc CLOEXEC?** Lai katrs Worker nepaturētu citu Worker galapunktus un neaizkavētu EOF atklāšanu. Tikai savam galapunktam pirms exec karogu noņem.
- **Kāpēc ignorē SIGPIPE?** Lai rakstīšana mirušam saņēmējam neizbeigtu Supervisor; send kļūda tiek apstrādāta kā errno.
- **Kāpēc vispirms iztukšo IPC, tad waitpid?** Worker var nosūtīt pēdējo RESULT/STOPPED un uzreiz beigties. Vispirms jāsaglabā šie ziņojumi, tikai pēc EOF jāpabeidz uzskaite.
- **Kas notiek avārijā?** Nepabeigtais darbs atgriežas rindā; dzīvie Worker turpina, avārijas fakts saglabā kodu 2.
- **Kas ir RAII?** Fd objekta destruktors aizver piederošo FD; Children destruktors ārkārtas izņēmuma gadījumā nogalina un sagaida vēl piederošos bērnus.
- **Kāpēc testos ir Python?** Tikai ārēja pārbaudes programma. Iesniedzamā sistēmprogramma ir C++ un OS mehānismus izsauc tieši.
