# Penjelasan: Clixon Config vs VPP Actual State

## 🔍 Perbedaan yang Diamati

### Dari Config XML (Saved)
```xml
<interfaces>
  <interface>
    <name>HundredGigabitEthernet8a/0/1</name>          <!-- no config -->
  </interface>
  <interface>
    <name>HundredGigabitEthernet8a/0/0</name>
    <enabled>true</enabled>                            <!-- configured -->
  </interface>
  <interface>
    <name>BondEthernet0</name>
    <enabled>true</enabled>                            <!-- configured -->
  </interface>
</interfaces>
<!-- loop0, loop1, tap4096 NOT in config -->
```

### Dari VPP Actual State (`vppctl show int`)
```
BondEthernet0              3      up    9000/0/0/0
HundredGigabitEthernet8a/0/0  1      up    9000/0/0/0
HundredGigabitEthernet8a/0/1  2     down   9000/0/0/0
loop0                      5      up    9000/0/0/0   ← Ada di VPP, tapi tidak di config!
loop1                      6     down   9000/0/0/0   ← Ada di VPP, tapi tidak di config!
tap4096                    4      up    9000/0/0/0   ← Ada di VPP, tapi tidak di config!
```

## ❓ Mengapa Berbeda?

### 1️⃣ Clixon = Configuration Management (DESIRED STATE)
- Hanya menyimpan konfigurasi yang **Anda tentukan via Clixon CLI**
- Adalah **source of truth** untuk konfigurasi yang ingin Anda pertahankan

### 2️⃣ VPP = Actual Runtime (CURRENT STATE)
- Menampilkan **semua interface** yang sedang berjalan di VPP
- Bisa termasuk interface yang di-create secara manual atau dari boot sebelumnya

## 📊 Perbandingan Sumber

| Interface | Clixon Config | VPP Runtime | Penjelasan |
|-----------|---------------|-------------|-----------|
| **HundredGigabitEthernet8a/0/0** | ✅ enabled | ✅ up | Dikonfigurasi via Clixon (no shutdown) |
| **HundredGigabitEthernet8a/0/1** | ✅ listed | ❌ down | Ada di config tapi tidak diaktifkan |
| **BondEthernet0** | ✅ configured | ✅ up | Dibuat via Clixon (create bond) |
| **loop0** | ❌ NOT in config | ✅ up | Dibuat di VPP sebelumnya, bukan via Clixon |
| **loop1** | ❌ NOT in config | ❌ down | Dibuat di VPP sebelumnya, bukan via Clixon |
| **tap4096** | ❌ NOT in config | ✅ up | Interface internal VPP, bukan user-created |
| **local0** | ❌ NOT in config | ❌ down | Loopback internal VPP |

## 🎯 Penjelasan untuk Setiap Interface

### Loop0 & Loop1
```bash
# Ini mungkin dibuat dengan command:
# sudo vppctl create loopback interface
# atau dari testing sebelumnya

# Karena dibuat LANGSUNG via vppctl (bukan via Clixon),
# maka NOT SAVED ke Clixon config
```

### Tap4096
```bash
# Interface internal VPP untuk TAP bridge
# Auto-created oleh VPP untuk LCP functionality
# Tidak perlu di-manage via Clixon
```

## ✅ Behavior yang BENAR

Clixon memiliki 3 mode operasi:

### Mode 1: Pure Clixon Management
```
Clixon CLI → Configure
              ↓
           vpp_api.c (API layer)
              ↓
           vppctl (Execute)
              ↓
         Clixon Config XML ← SAVED
              ↓
         VPP Runtime State
```

### Mode 2: Manual VPP Commands (tidak ter-save)
```
Direct vppctl commands
              ↓
         VPP Runtime State (TEMPORARY)
              ↓
    Clixon Config XML ← NOT SAVED
```

### Mode 3: Mixed Mode (Current State)
```
Clixon Configured: ✅
  - HundredGigabitEthernet8a/0/0 (no shutdown)
  - BondEthernet0 (created)
  
VPP Manual/Previous: ❌ (tidak di track)
  - loop0 (dari testing sebelumnya)
  - loop1 (dari testing sebelumnya)
  - tap4096 (internal VPP)
```

## 🔄 Jika Ingin loop0 Di-save di Config

Cukup configure via Clixon CLI:

```bash
debian# configure terminal
debian(config)# interface loop0
debian(config-if)# enabled
debian(config-if)# commit
```

Maka loop0 akan:
1. ✅ Tersimpan di `vpp_config.xml`
2. ✅ Dipulihkan otomatis saat Clixon restart
3. ✅ Ter-manage oleh Clixon

## 📝 Kesimpulan

| Aspek | Status |
|-------|--------|
| **Config Save**: HundredGigabitEthernet8a/0/0, BondEthernet0** | ✅ CORRECT |
| **VPP State**: Menampilkan semua interface termasuk yang manual** | ✅ CORRECT |
| **Desain Clixon**: Only save yang dikonfigurasi via CLI** | ✅ INTENDED |
| **Perbedaan adalah NORMAL** | ✅ YES |

---

**TL;DR:** 
- **Clixon Config XML** = apa yang Anda manage via Clixon CLI
- **VPP show int** = semua interface yang sedang running (termasuk manual/temporary)
- Perbedaan = **NORMAL dan EXPECTED** ✅
