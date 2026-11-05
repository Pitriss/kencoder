# KEncoder: nezávislé tagové releasy CLI a GUI

KEncoder má dvě součásti se **samostatným Debian verzováním**:

| Git tag | Nově sestavené balíčky | Převzaté balíčky |
| --- | --- | --- |
| `v0.1.0` | encoder a kencoder; amd64 a arm64 | Žádné, pouze první release |
| `encoder-v0.1.1` | encoder; amd64 a arm64 | kencoder z minulého release |
| `kencoder-v0.2.0` | kencoder; amd64 a arm64 | encoder z minulého release |

Workflow `.github/workflows/debian-release.yml` reaguje **pouze na tagy**.
Nové balíčky vznikají z označeného commitu v nativních amd64/arm64 prostředích
Debianu 13 (`debian:trixie`). CLI nepotřebuje Qt6 pro vlastní sestavení,
GUI zase nekompiluje CLI. Každý release má čtyři `.deb` soubory, `SHA256SUMS`
a `VERSIONS.txt` se skutečnými verzemi v balíčcích a údajem o převzetí.

## Tagování

První release (oba balíčky, pouze jednou):

```bash
git tag v0.1.0 && git push origin v0.1.0
```

Následná změna CLI:

```bash
git tag encoder-v0.1.1 && git push origin encoder-v0.1.1
```

Následná změna GUI:

```bash
git tag kencoder-v0.2.0 && git push origin kencoder-v0.2.0
```

Tag vytvořte až po otestovaném commitu. Lokální commit sám nic nepublikuje.
Nevytvářejte nový společný `v*` release po prvním vydání. Podporované verze
tagu jsou `X.Y.Z` a volitelně `X.Y.Z-rc1` / `X.Y.Z-alpha1` (v Debianu
s verzí `~rc1` / `~alpha1`). Verze měněné komponenty musí růst podle
`dpkg --compare-versions`.

## Závislosti

GUI vždy obsahuje povinnou minimální závislost na CLI, např.:

```text
Depends: encoder (>= 0.1.0), ...
```

Před release GUI zvyšte soubor `packaging/ci/encoder-min-version`, pokud nově
vyžaduje vyšší CLI. Release pak **selže**, jestliže CLI balíček převzatý
z posledního release není dost nový. Nejprve tedy vydejte nové CLI a poté GUI.
Nový GUI balíček může fungovat i s novějšími CLI bez vlastního rebuildování.
Nepoužívejte `${binary:Version}` pro porovnání verzí dvou nezávislých součástí.

## Ověření a bezpečnost

- Oba nové buildy používají `dpkg-buildpackage -b -us -uc` s oddělenou
  Debian source staging oblastí. Lokální `debian/` v repozitáři zůstává
  funkční pro společný vývojový build.
- Workflow bere poslední **publikovaný** release odpovídající tagům projektu.
  Stáhne jeho `.deb` a ověří `SHA256SUMS`, potom beze změny kopíruje oba
  balíčky nedotčené součásti. Chybějící nebo chybné assety release zastaví.
- Před publikací musí být přesně jeden balíček pro každou kombinaci součásti
  a architektury. Ověřují se názvy, skutečná metadata `Package`, `Version`,
  `Architecture`, shoda verzí mezi architekturami, růst verze a závislosti.
- Při prvním společném tagu se předchozí release nepoužije. Při následných
  tagech se oba nové `.deb` pro změněnou komponentu skutečně sestaví.
- GitHub Release vytváří až poslední job po všech kontrolách. Pracovní strom
  ani `master` se v CI nepřepisují. Publish job je serializovaný, takže se
  různé verze nezveřejňují současně.
- Skripty nevykonávají žádné USB HID operace ani nedělají Git commit/push.

Pozor: GitHub Actions `concurrency` serializuje běhy, ale negarantuje pořadí
jejich spuštění při současném odeslání více tagů. Doporučuje se počkat na
úspěšný release každé komponenty a až pak odeslat další tag. Kontrola
rostoucích verzí zabrání vydání starší verze dané komponenty.

## Lokální validace

```bash
for f in packaging/ci/*.sh; do bash -n "$f" || exit 1; done; git diff --check
```

Sestavení před releasem v kořeni repozitáře:

```bash
make && ./build/cli/encoder -h >/dev/null && cmake -S gui -B build/gui && cmake --build build/gui -j"$(nproc)" && QT_QPA_PLATFORM=offscreen ./build/gui/kencoder --self-test
```

Dokumentace neimplikuje, že byl build na GitHub Actions nebo na reálném
hardware již ověřen. Release je nutné poprvé sledovat v Actions.
