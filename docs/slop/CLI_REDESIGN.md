# SushiRuntime CLI — Yeniden Tasarım Spec'i

> Durum: **Onaylanmış tasarım, henüz implemente edilmedi.**
> Amaç: Mevcut ince Python wrapper'ı (`sushiruntime.py` + `.sh`/`.bat` çiftleri)
> gerçek, paket olarak kurulan bir CLI'a dönüştürmek.

## Karar özeti (stack)

| Konu | Karar |
|---|---|
| Framework | **Typer** (type-hint tabanlı, ince komutlar) |
| Mantığın yeri | **Python'a taşınır** — `.sh`/`.bat` çiftleri silinir, `cmake`/`docker` doğrudan `subprocess`'ten çağrılır |
| Dağıtım | **pyproject.toml + pipx** — `console_scripts` entry-point ile `sr` gerçek komut olur; `install.sh`/symlink/wrapper'lar gider |
| Çıktı/UX | **rich** (renkli çıktı, tablo, progress, hata panelleri) |
| Config | Tek **TOML** (mevcut `config.cfg` Linux + `config.ini` Windows ikilisi birleşir) |

## Hedef paket yapısı

```
.tools/
  pyproject.toml
  sushiruntime/
    __init__.py
    cli.py            # Typer app + komut tanımları (ince)
    config.py         # config yükleme/validasyon (katmanlı: default → TOML → env → flag)
    console.py        # rich tabanlı çıktı
    services/
      project.py      # build/test/run/clean/doxygen mantığı
      docker.py       # docker mantığı
      discovery.py    # example/binary bulma (--sort)
```

Komutlar **ince** kalır (sadece argüman alır), iş `services/` katmanındadır → shell'den
bağımsız, test edilebilir.

## Komut ağacı

```
sr project build  --type {release|debug|relwithdebinfo}  --distributed  [--no-cuda] [--clean]
sr project test   --suite {unit|regression|functional|benchmark}  --distributed  [--filter '...']
sr project run    [target]  --sort  --distributed[=master|worker]   -- <app args>
sr project clean
sr project doxygen
sr docker {build|run} [--admin]
```

Suite/type seçimleri **Enum** olarak modellenir → Typer otomatik validasyon + `--help`'te
geçerli değerleri gösterir + geçersiz değeri reddeder.

## `--distributed` — her yerde tutarlı

`SUSHI_ENABLE_DISTRIBUTED` opsiyonu `Graph::Node` layout'unu (PUBLIC compile def)
değiştirir, bu yüzden açıp kapamak temiz reconfigure ister. Mevcut stamp mekanizması
(`build/.sushi_dist`) korunur.

- **build:** layer'ı açar (`SUSHI_ENABLE_DISTRIBUTED=ON`, stamp ile gerekirse reconfigure).
- **test:** distributed testlerini de kapsar.
- **run:** `--distributed=master|worker` → distributed build seçer **ve** uygulamaya role'ü
  forward eder (`--distributed=master`). Bare `--distributed` (role yok) sadece build seçer.

## Test mekanizması — CTest labels

Test kategorileri ayrı binary değil; `sr_functional_tests` tek executable içinde
`functional/{unit,regression,integration}` olarak derleniyor. Ayrıca `sr_benchmark_tests`
(stress + performance, `SUSHI_BUILD_BENCHMARKS` ile gated).

**Çözüm:** Kategori ayrımı GTest isim filtresiyle değil (unit suite'lerinin ortak öneki
yok, kırılgan), **CTest label** ile yapılır. CMake'te küçük bir dokunuşla her gruba label
atanır:

| `--suite` | Çalıştırılan |
|---|---|
| `unit` | `ctest -L unit` |
| `regression` | `ctest -L regression` |
| `functional` | `ctest -L functional` (tüm functional = unit+regression+integration) |
| `benchmark` | `ctest -L benchmark` (stress + performance) |

`--filter` verilirse ctest'e/gtest'e geçirilir (ör. `--gtest_filter='Steal.*'`).

Tek mekanizma tüm test tiplerini kapsar; ileride eklenen testler label sayesinde otomatik
dahil olur.

> Not: Bu, CMake'te **label eklemek** anlamına gelir (kod değişikliği değil, minik).


## Build stilleri — `--type`

`--type` → `CMAKE_BUILD_TYPE`:
- `release` (varsayılan) — `/O2 /fp:fast /arch:AVX2`
- `debug` — `-fsanitize=thread` aktif (CMakeLists `Debug|RelWithDebInfo` matcher'ı)
- `relwithdebinfo` — debug info + optimizasyon, sanitizer aktif

`--no-cuda` → `SUSHI_ENABLE_CUDA=OFF` (varsayılan ON).

## Silinecek / birleşecekler

- `.tools/sushiruntime.py` → `sushiruntime/` paketiyle değişir
- `.tools/project/*.{sh,bat}`, `.tools/docker/*.{sh,bat}` → Python `services/`'e taşınır
- `.tools/sr.sh`, `sr.bat`, `sushiruntime.{sh,bat}`, symlink → pipx entry-point
- `.tools/install.{sh,ps1}`, `uninstall.{sh,ps1}` → `pipx install` / `pipx uninstall`
- `config.cfg` + `config.ini` (+ template'ler) → tek `config.toml`

## Açık/ileri detaylar

- Config TOML şeması (Linux + Windows araç yollarını tek dosyada nasıl tutacağımız).
- "example" tanımının CMake tarafından kesin sorgulanması (target property ya da
  `examples/` dizininden gelme).
- Cross-platform oneAPI/VS env yükleme (şu an `setvars.{sh,bat}` source/call) Python'da
  nasıl yapılacak.

---

*Bu doküman tasarım kaydıdır; implementasyona "şimdilik yaptırtmayacağım" kararıyla
henüz geçilmedi.*
