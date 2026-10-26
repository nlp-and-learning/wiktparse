# Jednolite drzewo: jeden kształt JSON dla każdego Wikisłownika

Wersja polska `uniform-tree.md`; obie trzeba zmieniać razem.

Szkic formatu gałęzi `uniform-tree` (po v1.1.0). Każdy Wikisłownik, niezależnie
od własnego układu, daje strony tego samego kształtu, tak aby jeden program
czytał je wszystkie. Ten dokument opisuje kształt dla ludzi; schemat JSON
wynika z niego (`res/schema/uniform.schema.json`).

## Dlaczego

Do v1.1.0 strona zachowuje drzewo nagłówków samej wiki: en
`Etymology 1 > Noun > Translations`, pl `Definitions > ''rzeczownik''`
(znaczenia pod sekcją definicji, część mowy w linii kursywą), ru
`Семантические свойства > Значение`, ar znaczenia tuż pod nagłówkiem języka,
kbd i nia bez żadnej części mowy. Tłumaczenia stoją pod częścią mowy (en, fr,
de) albo na poziomie języka (pl, ru, it, nl). Każdy czytający musi znać każdy
układ.

Jednolite drzewo zachowuje jako stałe poziomy to, co wspólne dla wszystkich
wiki, a wszystko inne jako atrybuty.

## Poziomy

```
strona
└─ languages[]              po jednym na sekcję językową strony
   └─ entries[]             po jednym na część mowy (i grupę, i formę)
      ├─ senses[]           definicje, id "1", "1.2" (zagnieżdżenie jako id, nie poziomy)
      │  └─ examples[]      przykłady i cytaty znaczenia
      ├─ translations[]     tego wpisu, każde do id znaczenia, gdy wiki mówi, którego
      └─ relations{}        Synonyms, Antonyms, Hypernyms, Derived terms…: listy terminów
```

Tylko trzy poziomy: **język**, **wpis**, **znaczenie**. Własne nagłówki wiki
nie są poziomami: to, co mówią, staje się atrybutami wpisu.

### Dlaczego pod językiem część mowy, a nie etymologia

- Każda wiki podaje część mowy (nagłówek, szablon, linia kursywą) albo da się
  ją odczytać z szablonu odmiany; gdzie się nie da, wpis ma `pos: null`.
  Poziom jest zawsze obecny.
- Tylko niektóre wiki dzielą według etymologii lub homografu (en
  `Etymology 1/2`, es `Etimología 1`, fr `{{S|nom|fr|num=2}}`, ru `{{з|I}}`, de
  dwa nagłówki `{{Wortart|Substantiv|Deutsch}}` na stronie *Bank*, każdy z
  własnym `{{Herkunft}}`). Jako poziom byłaby w większości wiki pusta albo
  wymyślona. Jako atrybuty nic nie ginie: en *rock* to osiem wpisów
  (rzeczownik i czasownik Etymology 1, czasownik i rzeczownik 2, rzeczownik i
  czasownik 3, rzeczownik 4 i 5) z `group` 1, 1, 2, 2, 3, 3, 4, 5 i tekstem
  swojej sekcji Etymology jako `etymology`.

**Wpis** to więc grupa znaczeń jednej części mowy, jednej grupy, gdy wiki ją
podaje (etymologii, homografu, własnego numeru), i jednej formy, gdy wiki ją
podaje (pl *bić* i *bić się* to dwa wpisy, oba czasowniki). Jedna część mowy
może dać kilka wpisów (de *Bank* ławka i *Bank* bank: dwa rzeczowniki).

## Strona

| pole | znaczenie |
|---|---|
| `wiki` | Wikisłownik (`en`, `pl`, `zh_min_nan`) |
| `title` | tytuł strony |
| `ns` | przestrzeń nazw (0 główna; 110 Thesaurus w en…) |
| `source` | miejsce strony w dumpie: `[offset strumienia, numer strony w strumieniu]` |
| `languages` | sekcje językowe w kolejności strony |

## Język

| pole | znaczenie |
|---|---|
| `code` | kanoniczny kod języka (wiktlangs.sqlite), `null`, gdy nieznany |
| `language` | język tak, jak zapisany w nagłówku lub znaczniku (`język polski`, `{{langue|fr}}`, `Ingiliffaa`) |
| `native` | język wiki (hasło we własnym języku wiki) |
| `pronunciation` | IPA, pliki audio, podział na sylaby całej sekcji językowej, gdy wiki podaje je tam (en `===Pronunciation===` przed `===Etymology 1===`, pl linie `{{wymowa}}` bez id znaczenia) |
| `audio` | pliki dźwiękowe wymowy: `["Pl-woda.ogg"]` |
| `etymology` | etymologia całej sekcji, gdy wiki podaje ją tam i nie oznacza żadnej jej części dla wpisu |
| `entries` | wpisy |
| `notes` | sekcje całego języka (en `===Anagrams===`, pl `{{źródła}}`, References, See also…): `{sekcja: tekst}` |
| `media` | pliki sekcji językowej nieoznaczone dla żadnego znaczenia ani wpisu, jak `media` wpisu |

## Wpis

| pole | znaczenie |
|---|---|
| `pos` | część mowy, angielska nazwa sekcji z wiktsections.sqlite (`Noun`, `Verb`, `Proper noun`, `Han character`); `null`, gdy wiki jej nie podaje |
| `pos_also` | dalsze części mowy tego samego nagłówka lub linii (de `{{Wortart|Substantiv|Deutsch}}, {{n}}, {{Wortart|Toponym|Deutsch}}`: `pos` `Noun`, `pos_also` `["Toponym"]`) |
| `pos_written` | część mowy tak, jak zapisana (`''rzeczownik, rodzaj żeński''`, `{{S|nom|fr|num=2}}`, `{{-ana-|mg}}`) |
| `group` | własny numer wpisu lub jego grupy wpisów w wiki: en `Etymology 2`, es `Etimología 2`, fr `num=2`, ru `{{з|II}}` (miejsce homografu, gdy szablon nie ma numeru), pl `2` z `(2.1)` |
| `form` | forma, gdy wiki robi z niej osobny wpis (pl `bić się` obok `bić`) |
| `headword` | linia hasła lub szablon odmiany tak, jak zapisane (`{{en-noun}}`, `{{bn-বিশেষ্য|…}}`) |
| `grammar` | rodzaj, klasa odmiany i podobne tak, jak zapisuje je wiki (`rodzaj żeński`, de `{{f}}, ''Bänke''`) |
| `etymology` | etymologia tego wpisu jako tekst: jego własna sekcja (de `{{Herkunft}}` pod częścią mowy), sekcja jego grupy (en `Etymology 2`, homograf ru) albo linie etymologii języka oznaczone dla niego (pl `(2.1)`, fr `{{laé|fr|nom|2}}`); brak: ta języka |
| `pronunciation` | jak przy języku, gdy wiki podaje ją dla wpisu lub jego grupy (de `{{Aussprache}}`, en `====Pronunciation====` pod `===Etymology 2===`, pl linie `{{wymowa}}` z id); brak: ta języka |
| `audio` | jak przy języku, dźwięki wymowy wpisu |
| `senses` | definicje |
| `translations` | tłumaczenia |
| `see` | tłumaczenia na innych stronach (en `{{trans-see|…}}`): `{"title", "gloss"}` |
| `subpages` | podstrony tłumaczeń dołączone do tłumaczeń (en `water/translations`): `{"title", "source"}` |
| `relations` | `{"Synonyms": [{"term": "warownia", "sense": "1.1"}, …], …}`: sekcje rodzaju relation z wiktsections.sqlite i szablony relacji przy znaczeniach (en `#: {{syn|en|…}}`); `sense` – id znaczenia tak, jak podaje je wiki (pl `(1.1)`, de `[1, 6]`, ru miejsce linii `#`, en znaczenie, pod którym stoi szablon), `qualifier` – etykieta linii (pl `{{zdrobn}}`, ru `прилагательные=`) |
| `sections` | pozostałe sekcje wpisu jako tekst: `{sekcja: tekst}` (Usage notes, Inflection, Descendants…) |
| `lexeme` | leksem Wikidata wpisu (ru `{{Лексема в Викиданных|L110019}}`); ar `{{معجمية|L1480479|…}}` pokazuje jego znaczenia zamiast własnych |
| `media` | obrazy Wikimedia Commons (dźwięki są w `audio`): `{"file": "…", "caption": "…", "sense": "1.1"}` (pl `[[Plik:…|thumb|zamek (1.1)]]`, ru `{{илл|…|Замок [1]}}`) |

## Znaczenie

| pole | znaczenie |
|---|---|
| `id` | `"1"`, `"1.2"` (zagnieżdżenie) albo własne id wiki (pl `"1.1"`, de `"1a"`) |
| `text` | definicja jako wikitekst, bez wyjętych z niej przykładów |
| `examples` | `[{"text": "…", "source": "…"}]`: przykłady i cytaty znaczenia, z jego linii (en `#:`, fr `#*`, ru `{{пример|…}}`) albo z sekcji przykładów według id znaczenia (pl `{{przykłady}}` `(1.2)`, de `{{Beispiele}}` `[1]`); `source` – autor, dzieło i rok cytatu, gdy podane |

## Tłumaczenie

Jak do v1.1.0, ale teraz zawsze pod swoim wpisem, jako tablica posortowana
według `lang` (te bez kodu na końcu; terminy jednego języka w kolejności wiki):

| pole | znaczenie |
|---|---|
| `lang` | język tłumaczenia, kod kanoniczny; `null`, gdy nieznany |
| `lang_written` | język tak, jak zapisany, tylko gdy `lang` jest `null` (nazwy języków w dowolnym języku pokazuje czytający, z wiktlangs.sqlite) |
| `term` | przetłumaczone słowo lub fraza |
| `sense` | id znaczenia podane przez wiki (pl `1.1`, de `[1]`, fr) |
| `gloss` | glosa tabeli tłumaczeń (en `{{trans-top|domestic species}}`) |
| `gender`, `tr` | oznaczenia rodzaju, transliteracja |
| `image` | termin jako obrazek: pl hieroglify `[[Plik:Egyptian-mw.PNG\|link=mw]]` dają `term` `mw`, `image` `Egyptian-mw.PNG`; znak języka migowego bez `link=` ma plik jako termin |

Pola późniejszych przejść (wiktlink: `page`, `home`, `en`, `page_title`;
wiktglos: `sense_ids`, `sense_method`, `sense_score`) zachowują swoje
znaczenie.

## Co gdzie trafia

Każda część sekcji językowej trafia tam, do czego należy:

1. Według nagłówków: to, co stoi pod częścią mowy, należy do jej wpisu; to,
   co stoi pod en `Etymology N` albo homografem ru obok jego części mowy,
   należy do wszystkich ich wpisów.
2. Według oznaczeń w liniach: id znaczeń (pl `(2.1)`, de `[1]`) do wpisu
   znaczenia; oznaczenia wpisu (fr `{{laé|fr|nom|2}}`) do tego wpisu; listy
   relacji ru według miejsca: n-ta linia `#` do znaczenia n (`?` i `—` to
   brak).
3. To, co należy do całego języka, zostaje przy nim: `pronunciation`,
   `etymology`, `notes`.

To, co należy do kilku wpisów: wymowa i etymologia są kopiowane do każdego z
nich, relacje też (en Derived terms z `Etymology 2` obok jej rzeczownika i
czasownika; pl linie `{{pokrewne}}` bez id); tłumaczenia trafiają do jednego
wpisu (ostatni wiersz tabeli niżej); pozostałe sekcje stają się `notes`
języka.

## Jak układy wiki przechodzą na ten kształt

| układ wiki | wpisy z |
|---|---|
| en: `Etymology N > Noun > Translations` | każda część mowy każdej etymologii to wpis, `group` N |
| fr, es, it, nl, ja: nagłówki lub szablony części mowy | każda część mowy to wpis; fr `num=2`, es `Etimología 2`: `group` 2 |
| de: `Bank ({{Sprache|Deutsch}})`, nagłówek `{{Wortart|…}}` na część mowy i na homograf | każdy nagłówek `{{Wortart}}` to wpis z własnym `{{Herkunft}}` i `{{Aussprache}}`; jego sekcje według id `[1]` ich linii |
| pl: `{{znaczenia}}` z liniami `''rzeczownik…''`, id `(1.1)`, `(2.1)` | każda linia `''…''` to wpis, `group` 1, 2…; każda sekcja języka (`{{wymowa}}`, `{{przykłady}}`, `{{synonimy}}`, `{{etymologia}}`, `{{tłumaczenia}}`…) dzielona według id swoich linii |
| ru: homografy `{{з|I}}`, `Семантические свойства > Значение` | każdy homograf to wpis (`group`), część mowy z szablonu morfologii, jego sekcje pod nim |
| ar, ga, fy, hu (znaczenia tuż pod językiem) | jeden wpis, `pos` z szablonu linii hasła, gdy znany, inaczej `null` |
| kbd, nia, te (bez części mowy) | jeden wpis, `pos` z szablonu (kbd `{{псалъ-лъэпкъ|…}}`, nia `{{nom}}`) albo `null` |
| tłumaczenia na poziomie języka (pl, ru, it, nl, zh) | do wpisu id znaczenia (pl `(1.1)` → grupa 1); jeden wpis: do niego; inaczej do wpisu, za którym stoją, inaczej do pierwszego |

## Co zostaje bez zmian

- Znaczniki języków, nazwy sekcji, kody: wiktlangs.sqlite, wiktsections.sqlite,
  `res/wiktionary/<wiki>.json`, bez zmian.
- Wikitekst znaczeń, przykładów i sekcji zostaje tak, jak zapisany; linki
  rozwiązuje się później (wiktlink).
- `source` wskazuje dump: oryginał jest zawsze pod ręką.

## Co się zmienia

- Brak drzewa własnych nagłówków wiki: stałe poziomy język, wpis, znaczenie.
- Tłumaczenia i relacje są zawsze pod wpisem, przykłady pod swoim znaczeniem.
- Sekcje trafiają tam, do czego należą (zob. *Co gdzie trafia*), nie według
  nazwy.
- Nowy format jest pisany prosto z dumpa (nowa gałąź), nie z JSONL z v1.1.0.

## Otwarte pytania

1. `sections` wpisu jako tekst czy rozbierane dalej (tabele odmiany) –
   najpierw tekst, rozbiór według rodzaju później.
2. Strony tezaurusa (en `Thesaurus:water`): osobne wpisy czy relacje hasła
   głównego – osobne wpisy, `ns` 110.
3. Tłumaczenia należące do kilku wpisów (en `====Translations====` pod
   `===Etymology N===` obok jego części mowy, rzadkie): do jednego wpisu czy
   do każdego – do jednego, jak w tabeli.
