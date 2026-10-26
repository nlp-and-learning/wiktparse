# Jednolite drzewo: jeden kształt JSON dla każdego Wikisłownika

Wersja polska `uniform-tree.md`; obie trzeba zmieniać razem.

Szkic formatu gałęzi `uniform-tree` (po v1.1.0). Każdy Wikisłownik, niezależnie
od własnego układu, daje strony tego samego kształtu, tak aby jeden program
czytał je wszystkie. Ten dokument opisuje kształt dla ludzi; schemat JSON
wynika z niego (`res/schema/`).

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
   └─ entries[]             po jednym na część mowy (i etymologię, i formę)
      ├─ senses[]           definicje, id "1", "1.2" (zagnieżdżenie jako id, nie poziomy)
      ├─ translations[]     tego wpisu, każde do id znaczenia, gdy wiki mówi, którego
      └─ relations{}        Synonyms, Antonyms, Hypernyms, Derived terms… listy terminów
```

Tylko trzy poziomy: **język**, **wpis**, **znaczenie**. Własne nagłówki wiki
nie są poziomami: to, co mówią, staje się atrybutami wpisu.

### Dlaczego pod językiem część mowy, a nie etymologia

- Każda wiki podaje część mowy (nagłówek, szablon, linia kursywą) albo da się
  ją odczytać z szablonu odmiany; gdzie się nie da, wpis ma `pos: null`.
  Poziom jest zawsze obecny.
- Tylko niektóre wiki dzielą według etymologii (en `Etymology 1/2`, es
  `Etimología 1`, de jeden nagłówek na homograf, ru `{{з|I}}`). Jako poziom
  byłaby w większości wiki pusta albo wymyślona. Jako atrybut nic nie ginie:
  en *rock* to cztery wpisy (rzeczownik 1, czasownik 1, czasownik 2,
  rzeczownik 3) z `etymology` 1, 1, 2, 3.

**Wpis** to więc grupa znaczeń jednej części mowy, jednej etymologii, gdy wiki
ją podaje, i jednej formy, gdy wiki ją podaje (pl *bić* i *bić się* to dwa
wpisy, oba czasowniki). Jedna część mowy może dać kilka wpisów.

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
| `pronunciation` | IPA, pliki audio, podział na sylaby całej sekcji językowej, gdy wiki podaje je tam |
| `etymology` | tekst etymologii, gdy wiki podaje jedną dla całej sekcji |
| `entries` | wpisy |
| `notes` | sekcje języka nienależące do żadnego wpisu (References, See also, External links…): `{sekcja: tekst}` |

## Wpis

| pole | znaczenie |
|---|---|
| `pos` | część mowy, angielska nazwa sekcji z wiktsections.sqlite (`Noun`, `Verb`, `Proper noun`, `Han character`); `null`, gdy wiki jej nie podaje |
| `pos_written` | część mowy tak, jak zapisana (`''rzeczownik, rodzaj żeński''`, `{{S|nom|fr}}`, `{{-ana-|mg}}`) |
| `etymology` | etymologia tego wpisu: `{"n": 2, "text": "…"}`; `n` – numer podany przez wiki (en `Etymology 2`, ru `{{з|II}}`), `text` – jej sekcja etymologii |
| `form` | forma, gdy wiki robi z niej osobny wpis (pl `bić się` obok `bić`) |
| `group` | własny numer wpisu w wiki, do którego odnoszą się id znaczeń (pl `2` z `(2.1)`) |
| `headword` | linia hasła lub szablon odmiany tak, jak zapisane (`{{en-noun}}`, `{{bn-বিশেষ্য|…}}`) |
| `grammar` | rodzaj, klasa odmiany i podobne tak, jak zapisuje je wiki (`rodzaj żeński`) |
| `pronunciation` | jak przy języku, gdy wiki podaje ją przy wpisie |
| `senses` | definicje |
| `translations` | tłumaczenia |
| `relations` | `{"Synonyms": [...], "Hyponyms": [...], "Holonyms": [...]}`: rodzaj relation z wiktsections.sqlite |
| `sections` | pozostałe sekcje wpisu jako tekst: `{sekcja: tekst}` (Usage notes, Inflection, Declension, Examples…) |
| `lexeme` | leksem Wikidata, którego znaczenia wiki pokazuje zamiast własnych (ar `{{معجمية|L1480479|…}}`) |
| `media` | pliki Wikimedia Commons (obrazy, dźwięki) |

## Znaczenie

| pole | znaczenie |
|---|---|
| `id` | `"1"`, `"1.2"` (zagnieżdżenie) albo własne id wiki (pl `"1.1"`, de `"1a"`) |
| `text` | definicja jako wikitekst |
| `examples` | przykłady i cytaty znaczenia |

## Tłumaczenie

Jak do v1.1.0, ale teraz zawsze pod swoim wpisem:

| pole | znaczenie |
|---|---|
| `lang`, `lang_written` | język tłumaczenia: kod kanoniczny, tak jak zapisany |
| `term` | przetłumaczone słowo lub fraza |
| `sense` | id znaczenia podane przez wiki (pl `1.1`, de `[1]`, fr) |
| `gloss` | glosa tabeli tłumaczeń (en `{{trans-top|domestic species}}`) |
| `gender`, `tr` | oznaczenia rodzaju, transliteracja |

Pola późniejszych przejść (wiktlink: `page`, `home`, `en`, `page_title`;
wiktglos: `sense_ids`, `sense_method`, `sense_score`) zachowują swoje
znaczenie.

## Jak układy wiki przechodzą na ten kształt

| układ wiki | wpisy z |
|---|---|
| en: `Etymology N > Noun > Translations` | każda część mowy każdej etymologii to wpis, `etymology.n` = N |
| fr, es, it, nl, ja: nagłówki lub szablony części mowy | każda część mowy to wpis |
| de: `Wasser ({{Sprache|Deutsch}})` na homograf, `{{Wortart|…}}` | każdy nagłówek części mowy to wpis; drugi nagłówek homografu: `etymology.n` 2 |
| pl: `{{znaczenia}}` z liniami `''rzeczownik…''`, id `(1.1)`, `(2.1)` | każda linia `''…''` to grupa: wpis, `group` 1, 2…; tłumaczenia do wpisu grupy z ich id |
| ru: homografy `{{з|I}}`, `Семантические свойства > Значение` | każdy homograf to wpis (`etymology.n`), część mowy z szablonu morfologii |
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
- Tłumaczenia i relacje są zawsze pod wpisem.
- Sekcje niebędące wpisami stają się `notes` języka albo `sections` wpisu,
  według nazwy.
- Nowy format jest pisany prosto z dumpa (nowa gałąź), nie z JSONL z v1.1.0.

## Otwarte pytania

1. `sections` wpisu jako tekst czy rozbierane dalej (tabele odmiany,
   przykłady) – najpierw tekst, rozbiór według rodzaju później.
2. Listy relacji: same terminy (`["kość", "szkielet"]`) czy z id znaczeń, gdy
   wiki je numeruje (pl `(1.1) [[kość]]`) – z id znaczeń.
3. Strony tezaurusa (en `Thesaurus:water`): osobne wpisy czy relacje hasła
   głównego – osobne wpisy, `ns` 110.
