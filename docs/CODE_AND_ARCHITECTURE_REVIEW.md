# SushiRuntime — Code & Architecture Review

**Tarih:** 2026-07-22
**Kapsam:** Tüm C++ çekirdeği (`include/`, `src/`), Python CLI (`cli/`), testler (`tests/`), dokümantasyon (`README`, `docs/`, `CONTRIBUTING`), build sistemi (`cmake/`, `Dockerfile`, CI).
**Yöntem:** Altı bağımsız inceleme ekseni (çekirdek eşzamanlılık, graph/execution, distributed+güvenlik, dokümantasyon senkronu, test+CLI, performans). Her bulgu gerçek kod satırlarına karşı doğrulandı; en kritik üç bulgu ayrıca elle teyit edildi.

---

## 0. Yönetici Özeti

SushiRuntime, olgunluk seviyesi yüksek, mimarisi tutarlı bir SYCL tabanlı heterojen compute runtime'ı. **Çekirdek tasarım kararları güçlü:** Chase-Lev work-stealing deque ders kitabı doğruluğunda, USM bellek havuzu (magazine bank + Treiber stack + lock-free radix map) gerçekten üst düzey, "compile-once / replay-many" disiplini baştan sona tutarlı, distributed katmanının fencing-token ve idempotency mantığı sağlam. Dokümantasyon hacmine göre şaşırtıcı derecede doğru.

Ancak müfettiş gözüyle bakıldığında **üç kategoride ciddi risk** var:

| Kategori | Durum | En kritik bulgu |
|---|---|---|
| **Bellek güvenliği (eşzamanlılık)** | ⚠️ Kritik | EBR'de seq_cst fence eksikliği → use-after-free (`epoch_reclamation.hpp`) |
| **Bellek güvenliği (ağ)** | ⚠️ Kritik | Worker `BUFFER_PUSH` heap overflow + blob bounds integer overflow |
| **Doğrulanabilirlik (CI)** | ⚠️ Yüksek | Distributed katmanının tamamı ve sanitizer'lar CI'da hiç çalışmıyor |

**Toplam:** 3 kritik, 12 yüksek, ~25 orta, ~20 düşük öncelikli bulgu. Kritiklerin hepsi küçük, lokalize düzeltmeler.

**Sorulara kısa cevaplar:**
- **Dokümantasyon & kod sync mi?** Büyük ölçüde evet, ama üç noktada bozuk: silinmiş `cli/install.py` hâlâ 3 kılavuzda öneriliyor; GPU-backend refactoru hiçbir dokümana yansımamış; `ABOUT_PROJECT.md`'de uydurma bir "deterministic mode" özelliği var.
- **Unlinked feature var mı?** Evet, iki yönde. Kod'da olup dokümante edilmeyenler (`SR_GPU_BACKEND`, ROCm/Intel backend'leri) ve dokümante edilip kodda olmayanlar (determinism modu, `State::distributed()` yanlış "yok" deniyor).
- **Yanlış mantık / optimizasyon eksiği?** Var — aşağıda ayrıntılı. En etkilisi dispatch hot-path'inde global mutex çekişmesi (3 ayrı ajan bağımsız tespit etti).
- **Ne kadar hızlandırılabilir?** Dispatch-yoğun iş yüklerinde tahmini **2-5×** submit throughput (global kilitlerin kaldırılması), artı task başına ~2 heap allocation + mutex/cv uyandırması (release loglama) tasarrufu.
- **Ne kadar güvenlileştirilebilir?** Ağ protokolündeki 4 bellek-güvenliği açığı kapatılıp CI'a fuzz + sanitizer + distributed lane eklenerek belirgin biçimde. Şu an distributed katman güven sınırında memory-safe değil.

---

## 1. KRİTİK Bulgular

### C1 — EBR `enter()` seq_cst fence eksik → use-after-free
**Dosya:** `include/SushiRuntime/memory/epoch_reclamation.hpp:284-285`

`enter()` şunu yapıyor: `global_epoch_.load(acquire)` ardından `thread_slots_[id].epoch.store(current, release)`. Release store, thread'in **sonraki** veri-yapısı okumalarının bu store globalde görünür olmadan önce tamamlanmasını engellemez (x86 TSO'da store-buffer forwarding, ARM'da serbest reordering). Bu klasik Dekker-tipi hata.

**Senaryo:** Reader R `enter()` yapar, slot store'u store buffer'da bekler; R paylaşılan yapıdan X node'una pointer okur. Eş zamanlı writer W X'i unlink eder → `retire()` → `check_and_reclaim()` slotları tarar, R'nin epoch'unu göremez (hâlâ 0), `min_active = current` hesaplar, X'i free eder. R serbest bırakılmış X'e erişir. Folly, crossbeam gibi tüm production EBR'ler slot epoch'unu yayınladıktan sonra `seq_cst` fence koyar.

**Düzeltme:** `enter()`'da slot store'undan sonra `std::atomic_thread_fence(std::memory_order_seq_cst);`. Elle teyit edildi (satır 284-285: yalnızca acquire/release var).

---

### C2 — Worker `BUFFER_PUSH` heap/USM overflow (ağdan attacker-controlled memcpy)
**Dosya:** `src/distributed/worker_agent.cpp:320-326`

Cache girdisi `push.handle.size_bytes`'a göre tahsis ediliyor (satır 323-324) ama sonra `std::memcpy(e.ptr, push.data.data(), push.data.size())` — `push.data.size()` kadar kopyalıyor (satır 326). Bunlar **bağımsız iki wire alanı**. `handle.size_bytes = 16` ve 1 MiB'lık `data` blob'u içeren bir `BufferPushMsg` → 16 baytlık USM buffer tahsis edilir, 1 MiB kopyalanır → remote heap/USM overflow write. `size_bytes = 0` da `malloc_shared(0)` + büyük kopya verir.

Master tarafındaki eşdeğerleri clamp'li (`distributed_coordinator.cpp:229,272`: `if (n > e->handle.size_bytes) n = e->handle.size_bytes;`). Worker path clamp'i unutmuş — net bir tutarsızlık.

**Erişim:** Worker portuna erişebilen herhangi bir peer (auth yok) veya ele geçirilmiş master, tek bir hazırlanmış `BUFFER_PUSH` gönderir.
**Düzeltme:** Kopyayı `min(push.data.size(), e.size)`'a clamp'le; ikisi çelişiyorsa reddet. Elle teyit edildi.

---

### C3 — `ByteReader::get_blob` integer overflow → OOB read / remote crash
**Dosya:** `include/SushiRuntime/distributed/protocol.hpp:200`

`if (!ok_ || pos_ + bytes > data_.size())` — `pos_` `size_t`, `bytes` payload'dan okunan attacker-controlled `uint64_t`. `pos_ + bytes` taşar: `bytes ≈ 2^64 - pos_` ile toplam küçük bir değere sarar, kontrol geçer, sonra `data_.subspan(pos_, bytes)` ~2^64 uzunlukta span döner (C++17 shim `subspan` bounds check yapmaz; C++20 `std::span` precondition ihlalini UB sayar).

**Downstream:** `BufferPushMsg::deserialize` (satır 335) ve `TaskResultMsg::deserialize` (satır 305) `data.assign(blob.begin(), blob.end())` yapar → ~2^64 baytlık tahsis → yakalanmayan `bad_alloc`/`length_error` reader/pump thread'inden kaçar → `std::terminate`. Blob uzunluğu `MAX_FRAME_BYTES` çerçeve limitine dahil değil (o yalnızca dış çerçeveyi sınırlar). Master'da `TASK_RESULT`/`BUFFER_PUSH`, worker'da `BUFFER_PUSH` ile erişilir — kimlik doğrulamasız remote crash.

**Düzeltme:** `bytes > data_.size() - pos_` (toplama yok) veya `bytes > MAX_FRAME_BYTES` reddi. Elle teyit edildi.

---

## 2. YÜKSEK Öncelikli Bulgular

### Eşzamanlılık / bellek güvenliği

**H1 — Paylaşılan `EventPolling` üzerinde veri yarışı → run hang'i.** `include/SushiRuntime/scheduler/event_polling.hpp:201-207` + `src/scheduler/scheduler.cpp:369,373`. Tek bir `EventPolling` instance'ı tüm worker'lar tarafından atomik olmayan üyeler (`backoff_counter`, `stall_polls_` — düz `size_t`) üzerinden eşzamanlı mutate ediliyor. İki worker `backoff_counter == 1` okuyup ikisi de `backoff_counter--` yaparsa ~2^64'e sarar; ardından her `poll_events` erken-dönüş branch'ine girer, in-flight event'ler asla reap edilmez, successor'lar tetiklenmez, run asılır. Ayrıca semantik olarak da yanlış: backoff/stall state per-worker olmalı. **Düzeltme:** sayaçları `ActiveEventList`'e taşı veya `EventPolling`'i per-worker yap.

**H2 — Reentrant olmayan `EpochGuard` iç guard, dış guard korumasını iptal ediyor.** `epoch_reclamation.hpp:294-300`. `exit()` koşulsuz slot'a 0 yazıyor. Worker dispatch iterasyonu boyunca guard tutuyor (`scheduler.cpp:364`), `DependencyTracker::resolve` kendi guard'ını alıyor (`dependency_tracker.cpp:75`). Worker-executed bir task `resolve()` tetiklerse (host task'tan dinamik graph inşası), iç guard iterasyon ortasında çıkar, worker'ın slot'u 0 okur, başka thread'in `check_and_reclaim()`'i hâlâ traverse edilen `AccessNode`'ları free edebilir. Test bunu "safe and balanced" sanıyor — dengeli ama güvenli değil. **Düzeltme:** `EpochThreadLocalState`'e per-thread nesting counter (yalnız en dış enter/exit slot'a dokunur).

**H3 — `WorkloadCoordinator::set_strategy()` use-after-free yarışı.** `workload_coordinator.hpp:103-106`. "Çalışırken çağrılması güvenli" diye dokümante edilmiş ama `strategy_ = std::move(new_strategy)` eski stratejiyi caller thread'inde yok ederken `monitor_loop()` coordinator thread'inde `strategy_->evaluate_and_rebalance()` içinde olabilir. Mutex/atomic/RCU yok → yok edilmiş nesneye virtual call. **Düzeltme:** her iki tarafın aldığı mutex, veya `atomic<shared_ptr>`, veya stop()/start() gerektir ve dokümanı düzelt.

**H4 — Moved-from `State`/`Buffer` handle'ları sessizce dangle ediyor; liveness guard yakalayamıyor.** `api/graph/graph.hpp:330-349` ve `api/detail/step_machinery.hpp:73-106`. Fluent katmanı `State*`'ı nesne adresiyle yakalıyor ama `State`/`Buffer` movable; liveness token handle ile birlikte taşınıyor, `has_dangling_resource()` false kalır. `std::vector<State<float>> states; g.add(states[0], fn); states.push_back(...)` (reallocation) sonrası `resync()` moved-from nesneden okur → kernel device'ta null deref eder, hata yükselmez. Move ctor silinmemiş, tespit yok, dokümante değil.

**H5 — Paylaşılan tek `CompletionLatch`, eşzamanlılık koruması yok.** `src/execution/execution_engine.cpp:59-98,411-421`. `ExecutionEngine::execute()` reentrant değil ama `Runtime` sınırsız `Graph` dağıtıyor, hepsi tek engine paylaşıyor. İki thread'ten iki graph çalıştırıldığında B'nin `completion.reset()`'i A'nın pending completion'ını siler, A'nın sentinel'i B'nin waiter'ını uyandırır, `arm_for_execution()` B için `run_failed_`'i temizler → hang, çapraz-atfedilmiş hata veya kaybolmuş failure. En azından atomik "run in progress" assertion gerekli.

**H6 — `TaskGraph` başarısız `compile()` sonrası dangling `Node*` üzerinde yürüyor.** `src/graph/task_graph.cpp:95-121,594-598`. `~Impl` node stack'lerini free ediyor ama `tracker.reset()` çağırmıyor; "no root nodes" throw'u (satır 597) `reset()`'ten (satır 601) önce oluyor. Node'lar atılan `CompiledGraph` ile ölür ama tracker hâlâ `AccessNode.task` pointer'larını tutar; sonraki `add_node()` `resolve()`'de free edilmiş pointer'lara `add_dependency` yapar → use-after-free write. Ayrıca compile edilmeden atılan graph her region access başına bir `AccessNode` sızdırır.

### Ağ / robustluk

**H7 — Wire'dan valide edilmeyen tahsis boyutu → worker crash.** `worker_agent.cpp:323,240`. `sycl::malloc_shared(push.handle.size_bytes, …)` ham 64-bit wire boyutu, üst sınır yok. Devasa `size_bytes` tahsisi başarısız kılar (null/throw). `BUFFER_PUSH` path'inde ardından gelen `memcpy` null'a yazar → segfault; throw edilen SYCL exception `on_message`'da yakalanmaz → `std::terminate`.

**H8 — Poll path'inden kaçan SYCL exception → `std::terminate`.** `event_polling.hpp:216,227` + `scheduler.cpp:369-373`. `execute_task` try/catch'li ama `poll_events` herhangi bir try dışında çağrılıyor. `event.get_info()`/`event.wait()`'ten çıkan async/device hata `dispatch_loop`'tan kaçar, `std::thread` gövdesinden çıkan exception process'i terminate eder (run'ı `record_run_error` ile fail etmek yerine).

### Doğrulanabilirlik

**H9 — Distributed katmanının tamamı CI'da hiç çalışmıyor.** `cmake/ProjectOptions.cmake:29` (`SR_ENABLE_DISTRIBUTED` default OFF) + `.github/workflows/ci.yml:35-41` `-DSR_ENABLE_DISTRIBUTED=ON` geçmiyor. Protokol round-trip, e2e offload, TCP loopback, worker-death fallback, stale-writeback fencing, Jepsen soak — reponun en iyi ~1800 satır testi her CI run'ından derlenip çıkarılıyor.

**H10 — CI'da sanitizer lane yok.** CI yalnızca Release build ediyor; ASan ağacı sadece lokal `sr build -t asan` ile var. `test_memory_pool.cpp:237-244` gibi ASan için tasarlanmış UAF/race testleri CI'da atıl. TSan hiç yok — C1/H1/H2 gibi bellek-sıralaması hataları tam da TSan'ın yakalayacağı türden.

**H11 — Wire protokolü için malformed-input/fuzz testi yok.** `test_distributed.cpp` yalnızca iyi-biçimli mesajları round-trip ediyor. Truncated buffer, oversized length prefix, garbage `MsgType`, hostile peer — C1/C3/H7'nin yaşadığı yüzey test edilmiyor.

**H12 — Python CLI'da hiç test yok + `sr --help` checkout dışında çöküyor.** `console.py:14-15` import-time'da `config_dir()` → `find_project_root()` çağırıyor, bu `CMakeLists.txt` atası yoksa `SystemExit` fırlatıyor. `cli.py` import anında `console`'u yüklediği için `sr --help`, `sr toolchain` dahil her çağrı Typer arg'ları parse etmeden "Not inside a SushiRuntime project" ile ölüyor. **Düzeltme:** console/config lazy init. Ayrıca repoda hiç `test_*.py` yok.

---

## 3. Dokümantasyon ↔ Kod Senkronu & Unlinked Feature'lar

Genel durum: **hata modu "gecikme", "uydurma" değil** — ARCHITECTURE.md'nin "burada adı geçen her sınıf/sabit/algoritma mevcut" vaadi ~40 nokta kontrolünde tutuyor. Bozukluk üç bölgede yoğunlaşmış.

### 3.1 Uydurma dokümanlar (dokümante ama kodda yok)

| # | Doküman | İddia | Gerçek | Önem |
|---|---|---|---|---|
| F1 | `CONTRIBUTING.md`, `CLI_GUIDE.md`, `INTRODUCTION.md` | `python cli/install.py` | Dosya `8bd4a6c` commit'inde silindi; README `ss install-cli` olarak güncellendi, diğer 3 doküman güncellenmedi | **Yüksek** |
| F2 | `docs/ABOUT_PROJECT.md` | "deterministic execution mode (task order + FP reduction order sabit)" | **Böyle bir mod yok** — API/flag/kod yok, yalnızca WP-4 roadmap | **Yüksek** |
| F3 | `ABOUT_PROJECT.md` | "backend AdaptiveCpp'tir" | AdaptiveCpp ikincil; birincil intel-llvm (kendi içinde çelişiyor) | Orta |
| F4 | `DISTRIBUTED_GUIDE.md` §5 | "`State<T>::distributed()` **yok**" | **Var** — `api/data/state.hpp:211-224`; README doğru dokümante ediyor | Orta-Yüksek |
| F5 | `CONTRIBUTING.md` §5 | env flag `SUSHI_ENABLE_DISTRIBUTED` | Böyle env yok; gerçek switch CMake `SR_ENABLE_DISTRIBUTED` | Orta |
| F6 | `README`, `ARCHITECTURE.md` §14 | `-DSR_ENABLE_CUDA=ON` ana yol | Artık legacy alias; gerçek knob `SR_GPU_BACKEND` (auto/cuda/rocm/intel/cpu/none) | Orta |
| F7 | `CHANGELOG.md` | `sr project doxygen` / `sr project test` | Komut grubu flatten edildi; gerçek: `sr doxygen` / `sr test` | Düşük-Orta |
| F8 | `TODO.md`, `ABOUT_PROJECT.md` | `docs/slop/cpp_standard_migration.md` | Ne o ne hedef dosya var | Orta |
| F9-F12 | çeşitli | §11 vs §12 off-by-one, kırık relative link'ler, `SUSHI_*` vs `SR_*` env, install snippet | — | Düşük |

**Stale intent (TODO.md hâlâ açık gösteriyor ama düzeltilmiş):** event-cap drop (#30), RTTI downcast (#33), "No DAG cancellation" (#37 — `Graph::cancel()` var), Fluent dependency tracking (Option A shipped), RunReport reserved fields (artık dolduruluyor).

### 3.2 Unlinked feature'lar (kodda var, dokümanda yok)

| # | Özellik | Konum | Önem |
|---|---|---|---|
| U1 | **`SR_GPU_BACKEND`** + vendor auto-detect, **ROCm/AMD backend**, **Intel GPU backend**, off-PATH nvcc/hipcc keşfi | `cmake/gpu/*.cmake` | **Yüksek** — mevcut GPU hikâyesi tamamen dokümantasyonsuz |
| U2 | `SR_BUILD_TESTS` (default OFF) | `cmake/ProjectOptions.cmake:35` | Orta — README "direkt CMake build" sonra `ctest` diyor ama testler build edilmemiş olur |
| U3 | `Errc::invalid_access`, `Errc::dangling_resource` | `core/error.hpp:50-51` | Orta |
| U4 | `Core::Delegate`, `Core::StrongId`, `Core::Liveness` | `include/SushiRuntime/core/` | Düşük-Orta |
| U5-U7 | CLI env override'ları (`SR_CC`/`SR_CMAKE`/…), `sr build` ASan fallback + stale-tree tespiti, DynamicGraph offload caveat | — | Düşük |

**Not:** Prompt'ta şüphelenilen `thermal_proxy_strategy` ve `remapping_topology_provider` aslında **dokümante** (README + ARCHITECTURE §9.5/§6).

### 3.3 CHANGELOG

`[Unreleased]` son ~10 commit'i kaçırıyor (sushicli entegrasyonu, install.py kaldırma, modüler cmake/GPU backend'leri, nvcc keşfi, rc_exe). Bu CONTRIBUTING §5'in "hiçbir user-visible değişiklik changelog'u atlayamaz" kuralını ihlal ediyor. Yapısal kusur: `[Unreleased]` içinde **duplicate `### Added` ve `### Changed`** başlıkları (satır 11/121 ve 81/195).

---

## 4. Performans — Sıralı Fırsatlar

> Not: P0-1 (global `pending_mutex`) üç ayrı ajan tarafından bağımsız tespit edildi — yüksek güven.

### P0 — Hot-path, yüksek değer

**PERF-1 — Global `pending_mutex` dispatch başına 2× alınıyor.** `src/scheduler/scheduler.cpp:270-271,343,375-382`. Her worker her dispatch-loop iterasyonunda priority-queue kontrolü (375) + drain (270) için koşulsuz global mutex alıyor — priority'ler nadir olsa bile. Steady-state'te task başına ~2N kilit alımı, tek cache line tüm çekirdekler arası ping-pong. Chase-Lev deque'lerin lock-free avantajı bu satırda boğuluyor. **Düzeltme:** iki relaxed atomik sayaç (`pending_count_`, `priority_count_`), lock-free kontrol, yalnız sıfırdan farklıysa kilit al (~20 satır). **Tahmini etki:** kısa task'larda 8-16+ worker'da **2-5× dispatch throughput**; uzun task'larda (>50µs) etki azalır.

**PERF-2 — Global `usm_owner_mtx` + hash lookup her dispatch'te.** `scheduler.cpp:614` + `runtime_context.cpp:566-570`. Her IO-anchor'lı task `get_queue_for_ptr` çağırıyor, bu düz `std::mutex` + `unordered_map::find` yapıyor — dispatch başına, sonsuza dek serialize. **Düzeltme:** çözülen queue index'ini `Node`'a cache'le (`node.hpp:369`, `locality_checked_` yanına), veya `shared_mutex`/lock-free radix (allocator'da zaten var). PERF-1 ile birlikte submit→execute döngüsünü tamamen de-serialize eder.

**PERF-3 — Release build'de per-step INFO loglama.** `core/logger.hpp:61-62` (NDEBUG altında `ACTIVE_LOG_LEVEL = INFO`) + `execution_engine.cpp:355,429` (step başına 2 `SR_LOG_INFO`). Fluent replay her step'te `execute()` çağırdığı için milyon-step `run(n)` = 2M log record. Her record: `ostringstream` × birkaç + timestamp `snprintf` + global mutex + condvar notify = per-step birkaç heap alloc + kilit — tam da `CompletionLatch`'in "step başına allocation yok" hedefini bozan yerde. **Düzeltme:** iki per-step log'u `SR_LOG_DEBUG`'a indir (release'de `if constexpr` ile derlenip çıkar) ve/veya release seviyesini WARN yap. Neredeyse sıfır efor.

### P1 — Gerçek ama daha küçük / belirsiz

- **PERF-4 — `worker_completed_tasks[]` false sharing + initialize edilmiyor.** `task_scheduler.hpp:148`. Dizi `alignas(64)` ama elemanlar 8 bayt arayla → 8 worker her cache line'ı paylaşıyor; ayrıca constructor sıfırlamıyor, C++17'de belirsiz değer → `ThermalProxyStrategy` çöp offset'lerle spurious migration yapar. `WorkerTimeBuckets` (satır 158-165) bunu doğru yapıyor — model var, uygulanmamış. **Düzeltme:** per-element `alignas(CACHE_LINE_SIZE)` struct + `{0}` init.
- **PERF-5 — Poller'da `sycl::event{}` temporary + per-completion `wait()`.** `event_polling.hpp:225-228`. DPC++'ta default-construct edilen `event` heap alloc yapar, `wait()` bilinen-tamamlanmışta bile driver çağrısı. **Düzeltme:** explicit `bool has_event`; toolchain-gate'li `wait()` skip.
- **PERF-6 — Hiçbir yerde LTO/IPO yok.** `cmake/**`'te `INTERPROCEDURAL_OPTIMIZATION`/`-flto`/`/GL` yok. Hot path virtual-call yoğun ve TU'lara bölünmüş → devirtualization headroom'u var. **Düzeltme:** `check_ipo_supported()` guard'lı `CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON` (SYCL device pass ile uyumu test et). Etki belirsiz: 0-15%, denemesi ucuz.
- **PERF-7 — Dependency-tracker reader-heavy graph'ta O(N²) compile.** `dependency_tracker.cpp:113-127`. Bir buffer'ın N reader'ı her biri diğer reader'ları geçerek yürüyor. Compile-only (replay ile amortize), yalnız 10k+ paylaşılan-buffer task'ta ısırır. **Düzeltme gerekirse:** ilk covering writer'da veya önceki read epoch'unda dur; interval tree.
- **PERF-8 — Küçük dispatch-path allocation'ları.** `SmallEventVector::to_vector()` (scheduler.cpp:524) dispatch başına taze vektör; `RangeTask::split()` (task.hpp:188) `std::function`'ı kopyalar (steal başına heap alloc); `drain_pending_submissions` (satır 267) drain başına vektör kilit tutarken. **Düzeltme:** span/small-vector view'lar.

### Bellek alt sistemi notları
- **NUMA allocator'da dekoratif, scheduler'da gerçek.** `MemoryPage::numa_node_id` saklanıyor (`usm_allocator.cpp:776`) ama sayfalar düz `malloc_shared` ile geliyor (satır 764), hwloc membind/first-touch yok. Scheduler tarafı (steal-victim NUMA tier'ları) gerçekten topolojiyi kullanıyor. 2+ soket makinelerde havuz sayfalarını bind etmek eksik parça.
- **Havuz yalnız ≤4KB.** `usm_allocator.cpp:532-533` bucket'lar {128,256,1024,4096}. >4KB direkt `malloc/free`; 4KB-1MB orta boy churn havuzsuz. 256→1024 sıçraması 257-1024B için ~4× iç fragmentasyon.
- **Deque ayak izi:** worker başına sabit 2MB ring (256K slot), 16 worker'da 32MB eager. Bilinçli trade-off ama not değer.

### İyi optimize edilmiş (dokunmayın)
1. **Chase-Lev deque** — ders kitabı memory ordering (Lê et al. 2013 fence yerleşimi), 64-bit monoton index (ABA yok), cache-line ayrılmış top/bottom, owner-private overflow buffer task düşürmez.
2. **USM pool** — sıfır-atomik thread-local magazine, capped bulk claim, intrusive remote-free Treiber stack, TCMalloc-tarzı lock-free radix page map, kendi RetiredNode'unu havuzlayan EBR.
3. **Compile-once/replay-many** — reusable `CompletionLatch` (promise/future yerine), sentinel bir kez wire, inline successor/event storage, RTTI-free hot-path downcast (`is_graph_node()`), off iken sıfır clock sample profiling.

---

## 5. Orta / Düşük Öncelikli Diğer Bulgular

### Doğruluk (orta)
- **M-corr1 — Completed-task sayaçları EBR slot id ile index'leniyor, worker id ile değil.** `scheduler.cpp:501-503` vs `832-836`. EBR'ye önce dokunan non-worker thread (graph compile eden main thread) tüm worker'ların EBR id'sini kaydırır → `ThermalProxyStrategy` yanlış worker'ın sayacından rate hesaplar, sağlıklı worker'lardan task migrate eder.
- **M-corr2 — Pool sayfaları `malloc_shared` ile, alignment sözü ihlal edilebilir.** `usm_allocator.cpp:764,345`. `alignment ≤ 128` istekleri havuza gidiyor ama slab yalnız temel alignment garanti ediyor; 64/128-align isteyen caller 16-align pointer alabilir. **Düzeltme:** `aligned_alloc_shared`.
- **M-corr3 — `node.hpp:417-425` `add_successor` count'u slot yazmadan önce publish ediyor.** Concurrent reader artmış count'u görüp henüz-yazılmamış (null) slot'u okur, atlar → successor'ın `in_degree`'si düşmez, deadlock. Bugün yalnız "mutasyonlar step boundary'de" konvansiyonu kurtarıyor.
- **M-corr4 — Key resolution iki farklı zamanda (`Reads/Writes` frozen vs State-rule rebuild).** `graph.hpp:255-269` vs `332-342`. Graph mid-lifetime mutate + tek sayıda `advance()` sonrası rebuild → reader stale key alır, RAW edge kaybolur veya spurious serialization. Sessiz yanlış sonuç.
- **M-corr5 — `op_support` "any-worker" semantiği yorumla çelişiyor + dispatch worker kapasitesini kontrol etmiyor.** `distributed_coordinator.cpp:69-70,442-447` + `offload_policy.cpp:61-99`. Heterojen kernel set'te task kernel'i olmayan worker'a gider → fail → re-dispatch churn.
- **M-corr6 — `~Impl`'de `q.wait_and_throw()`.** `runtime_context.cpp:170-173`. Son run'da geç async device hata → Runtime yıkımında `std::terminate`.
- **M-corr7 — `probe_hardware()` re-run `usm_owner` index'lerini geçersiz kılıyor.** `runtime_context.cpp:290-292`. Public API re-probe'a davet ediyor ("topology değişince çağır") ama owner index'leri yeniden yapılanmış queue'lara işaret eder → çapraz-context dispatch/free.
- **M-corr8 — Buffer/State tahsis başarısızlığı sessiz.** `buffer.hpp:123-136`. OOM'da `nullptr` saklanıyor, throw yok; `dependency_key()` null → tracker region'ı atlar → hem ordering kaybı hem kernel null deref.
- **M-corr9 — Zayıf cycle detection.** `task_graph.cpp:594-598`. `compile()` yalnız *sıfır* root'ta throw ediyor; kısmi cycle + bağımsız root temiz compile eder, `completion.wait()` timeout'suz sonsuza asılır.
- **M-corr10 — `TaskMetadata::name` ham pointer wire'a serialize ediliyor.** `protocol.hpp:242,255`. Master'ın `name` pointer'ı worker adres uzayında yeniden kurulur (wild pointer) — protokolün "pointer serialize etmeyiz" invariant'ını ihlal eder. Worker şu an dereference etmiyor (latent).

### Robustluk / güvenlik (orta)
- **M-sec1 — Connection-slot DoS.** `tcp_transport.cpp:247-258`. HELLO/read timeout yok (`SO_RCVTIMEO` hiç set edilmiyor), `read_n` sonsuza bloke. Bağlanıp hiç göndermeyen peer worker slot'unu sonsuza işgal eder.
- **M-sec2 — Kimliksiz buffer writeback keyfi master buffer'a ID ile.** `distributed_coordinator.cpp:208-231`. ID'ler 1'den sıralı (tahmin edilebilir); peer'in o buffer'ı üreten task'ı aldığı kontrol edilmiyor. Trusted-worker modelinde by-design ama not değer.

### Test kalitesi (orta/düşük)
- **T1 — Stress test double-hand-out tespit edemiyor.** `test_memory_stress.cpp:43-82`. Header "no double-hand-out" diyor ama her thread yalnız kendi private vektöründe `ptr != nullptr` kontrol ediyor, `SUCCEED()` ile bitiyor. İki thread aynı pointer'ı alsa yeşil kalır. **Düzeltme:** join sonrası global uniqueness assert.
- **T2 — Tautological testler.** `test_affinity.cpp:40-108` (yalnız "throw etmiyor", affinity uygulandığını hiç doğrulamıyor), `test_logger.cpp:37-52` (`EXPECT_NO_THROW`, output capture yok).
- **T3 — Flaky exact-split.** `test_distributed_e2e.cpp:422-423` `c0 == 1 && c1 == 1` tam eşitlik istiyor — yüklü CI'da ilk task ikinci dispatch'ten önce biterse ikisi tek worker'a gider. **Düzeltme:** `c0+c1==2 && offloaded==2` veya deterministik latch.
- **T4 — Wall-clock budget assertion'ları** (`test_fluent_api.cpp:144-148`, priority-bypass heuristic'leri) CI çekişmesinde flake riski.
- **T5 — Windows CI yok** (birinci-sınıf Windows desteğine rağmen); allocator failure path'leri (OOM, foreign free, double-free) test edilmiyor; benchmark sayıları saklanmıyor/baseline ile karşılaştırılmıyor (regresyon otomatik yakalanmıyor).

### CLI (orta/düşük)
- **CLI1 — Stale-configure bug.** `services/project.py:205-219`. `_needs_configure` yalnız generator sentinel + distributed stamp'i kontrol ediyor; `-t release` sonra `-t debug` → reconfigure atlanır, single-config Ninja'da kullanıcı sessizce Release binary alır. **Düzeltme:** stamp'e `(dist, build_type, gpu_backend, tests)` kodla.
- **CLI2 — cmd-string ile config-türevi path çalıştırma.** `env.py:96-104`. `vs_vcvars`/`oneapi_root` içinde `&`/`^`/`"` (Windows dizin adlarında legal) quoting'i kırar. **Düzeltme:** temp `.bat`.
- **CLI3 — egg-info git'e commit'li.** `cli/sushiruntime_cli.egg-info/*` (6 dosya) tracked; `.gitignore:29` kuralı sonradan eklenmiş, gitignore untrack etmez. **Düzeltme:** `git rm -r --cached cli/sushiruntime_cli.egg-info`.
- **CLI4 — Kırık install talimatı.** `pyproject.toml:21-22` + `CONTRIBUTING.md:35` `cli/install.py`'ye referans veriyor ama dosya yok; ayrıca hard `sushicli` bağımlılığı `pyproject.toml`'da yok → `pip install -e cli` sonrası `ModuleNotFoundError`.
- **CLI5 — Diğer:** env cache reconfigure'da hemen siliniyor (`project.py:421-427`), child env'de PATH ikiye katlanıyor (`env.py:44-65`), `test()` `CUDA_LAUNCH_BLOCKING`'i strip etmiyor ama `run()` ediyor, `SYCL_LIB_SUFFIX=9` hardcoded pin.

### Düşük öncelikli çekirdek
- Allocator destructor'ı çakışan canlı allocator'ın registry slot'unu null'lar (`usm_allocator.cpp:263`, `id % 256`) → uzun-ömürlü process'te havuz sızıntısı; **düzeltme:** `compare_exchange_strong`.
- `USMManagerImpl` `sycl::queue&` referans üyesi tutup destructor'da kullanıyor (`usm_allocator.cpp:526,295`) → queue önce yıkılırsa UAF; **düzeltme:** value ile sakla.
- `LockFreePageMap` 50-bit VA maskeliyor (`usm_allocator.cpp:98-107`), LA57 host'ta aliasing.
- `get_bucket_index` `__builtin_clzll`'i doğrudan kullanıyor (`usm_allocator.cpp:615`), projenin `Core::bit.hpp` MSVC shim'ini bypass ediyor → cl.exe build kırılır.
- `scheduler.cpp:143-151` `start()` çift-çağrı guard'ı yok → Chase-Lev single-owner invariant'ı sessizce bozulur.
- Armed-idle pool asla bloke olmuyor, 100µs granülaritede busy-loop (`scheduler.cpp:343-357`) → run'lar arası boşuna mutex/steal trafiği.
- `is_fused` flag'i (`node.hpp:333`) hiç yazılmıyor/okunmuyor ama `task_graph.hpp:61` "kernel fusion yapabilir" diyor — reklamı yapılan ama var olmayan özellik.
- `build_run_report` tüm task'ları queue 0'ın device sınıfına atfediyor (`run_report_builder.hpp:70-75`) → CPU+GPU kutuda her host task GPU işi raporlanır.

---

## 6. Neler İyi Yapılmış (Denge İçin)

1. **Doğruluk mühendisliği:** RAW/WAR/WAW sub-region resolution'ı ince kısımlar dahil doğru (yalnız *tam kaplayan* önceki writer'da dur); failure/cancellation drain-to-sentinel tasarımı hata'da deadlock etmez, pool'u re-armable bırakır; first-exception-wins doğru.
2. **Distributed sistem muhakemesi:** her (re)dispatch output version'ı bump ediyor, iki writeback path'i de stale/superseded sonucu reddediyor (fencing token) + task-id idempotency dedup → zombie worker taze veriyi ezemez.
3. **Test rigor'u (bazı yerlerde olağanüstü):** `test_dependency_tracker.cpp` region-overlap matrisi her düşürülmüş-edge sınıfını tek satır observable ile kodluyor; Jepsen-lite harness (seeded nemesis, side-thread liveness); `test_allocator_perf.cpp` calibrated rdtsc tail + randomized round order + median-of-rounds + honest malloc baseline — bu ölçekte nadir.
4. **Temiz primitifler:** Chase-Lev deque, USM pool tasarımı, `Delegate` no-alloc type-erasure, temiz thread lifecycle (shutdown-before-join, CAS-guarded start/stop, doğru partial-read/write loop'ları).
5. **Dürüst threat model:** SECURITY.md TcpTransport'un kasıtlı auth/TLS'siz olduğunu doğru beyan ediyor ve wire protokolü memory-safety'sini açıkça kapsama alıyor — C1/C3/H7 tam da bu kapsamdaki gerçek hatalar.

---

## 7. Önerilen Eylem Sırası

**Hemen (bellek güvenliği — hepsi küçük, lokalize):**
1. C1: EBR `enter()`'a seq_cst fence.
2. C2: worker `BUFFER_PUSH` memcpy'ını `min(data.size(), e.size)`'a clamp'le.
3. C3: `get_blob` overflow-safe bounds (`bytes > data_.size() - pos_`).
4. H7: wire tahsis boyutuna üst sınır; H8: `poll_events`'i try/catch + `record_run_error`'a route et.

**Kısa vade (doğrulanabilirlik — hataların çoğu bu yüzden kaçmış):**
5. H9: CI functional lane'e `-DSR_ENABLE_DISTRIBUTED=ON`.
6. H10: ASan + TSan CI lane (suite zaten tasarlanmış).
7. H11: protokol için truncated/garbage/oversized deserialize testleri.
8. H12 + CLI3/CLI4: CLI lazy-init, egg-info untrack, minimal pytest, CI'a CLI import/lint job.

**Orta vade (performans — ölç, sonra uygula):**
9. PERF-3 (dakikalar), PERF-4 (dakikalar), sonra PERF-1 + PERF-2 (benchmark ile birer gün), ardından PERF-5/6 ölçüm arkasında.

**Doküman:** F1 (install.py'yi 3 dokümandan temizle), U1 (`SR_GPU_BACKEND` + ARCHITECTURE §14/README build), F4 (`State::distributed()`), F2/F3 (ABOUT_PROJECT determinism/AdaptiveCpp), CHANGELOG'u yakala + duplicate başlıkları temizle.

**Eşzamanlılık doğruluğu (H-serisi):** H1 (per-worker EventPolling), H2 (reentrant EpochGuard), H3 (set_strategy senkronizasyonu), H4/H6 (fluent handle move + tracker reset), H5 (engine reentrancy assertion).
