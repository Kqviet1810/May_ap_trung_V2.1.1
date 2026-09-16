# Báo mất điện độc lập qua ATtiny13A

Bổ sung sau audit v3.7.1: khi ESP32 mất điện, còi do ESP32 điều khiển
(`PIN_OUT_SIREN`, GPIO47) cũng mất điện theo — vô dụng đúng lúc cần báo nhất.
Giải pháp: chuyển việc báo mất điện sang một chip **ATtiny13A-PU** chạy hoàn
toàn độc lập, có nguồn riêng backup bằng pin cúc áo CR2032, nên vẫn sống dù
ESP32 và cả nguồn 9V chính đều mất.

Sơ đồ gốc: `Schematic_ESP32_MAT_HMI_V1.pdf` (do người dùng cung cấp,
2026-09-16), linh kiện `U6`.

## 1. Ba khối mạch liên quan

**Nguồn riêng cho ATtiny** — net `3.3V` (khác `3.3V_ESP` của ESP32), có pin
CR2032 (B2) + tụ lọc C2/C4. Chỉ cấp cho ATtiny13A, không liên quan gì đến
ESP32 hay nguồn 9V.

**Mạch chia áp đo lường** (đưa về 2 chân ADC của ATtiny):
- `3.3V_ESP` → R1(2.2M) → *node* → R2(3.3M) → GND, *node* nối `PB2`/ADC1 qua
  R3(2.2M) (chỉ để lọc/bảo vệ, không đổi tỉ lệ chia vì ADC gần như không hút
  dòng). Tỉ lệ chia: `R2/(R1+R2) = 3.3M/5.5M ≈ 60%`. Lúc đầy đủ điện
  (3.3V_ESP=3.3V), điểm đo ≈ **1.98V**.
- `9V` → R5(3.3M) → *node* → R6(1M) → GND, nối `PB3`/ADC3 qua R4(2.2M). Tỉ lệ
  chia: `R6/(R5+R6) = 1M/4.3M ≈ 23%`. Lúc đầy đủ 9V, điểm đo ≈ **2.09V**.
  **Chưa dùng trong firmware hiện tại** — để dành mở rộng sau (vd báo mức
  pin/nguồn 9V qua bus khi ESP32 còn sống).

Điện trở cỡ MΩ → dòng tiêu thụ mỗi nhánh dưới 1µA, không đáng kể so với tuổi
thọ pin CR2032.

**Còi báo** — `PB1` qua R7(100k) kích cực B của Q2 (NPN), Q2 đóng/cắt còi
(U21) dùng nguồn **9V** (không phải 3.3V) — nên còi vẫn kêu được dù ESP32 đã
chết, miễn nguồn 9V/pin dự phòng còn.

## 2. Bảng chân ATtiny13A (DIP8)

| Pin vật lý | Tên | Vai trò trong mạch |
|---|---|---|
| 1 | PB5 / RESET | Kéo lên 10k (R10) — giữ nguyên vai trò RESET, **không** dùng làm GPIO |
| 2 | PB3 / ADC3 | Đo điện áp 9V (chưa dùng trong firmware) |
| 3 | PB4 | Nối thẳng lên 3.3V — không dùng, tránh thả nổi |
| 4 | GND | — |
| 5 | PB0 / MOSI | Bus 1 dây, đọc trạng thái "đang ấp" từ ESP32 (GPIO41) |
| 6 | PB1 / MISO | Điều khiển còi (qua Q2) |
| 7 | PB2 / SCK / ADC1 | Đo điện áp 3.3V_ESP |
| 8 | VCC | Nguồn `3.3V` riêng, backup CR2032 |

Chân ISP (J1, đầu nạp 2x3 chuẩn AVR: MOSI/MISO/SCK/RESET/GND) dùng chung với
PB0/PB1/PB2/PB5 — bình thường không xung đột vì ISP chỉ hoạt động lúc nạp
code, không chạy song song với firmware ứng dụng.

## 3. Giao thức bus 1 dây (PB0 ↔ ESP32 GPIO41)

**Cố tình đơn giản hoá tối đa** — không phải giao thức 1-Wire chuẩn (không có
timing µs chặt chẽ), chỉ là một **mức tín hiệu DC** được ESP32 làm mới liên
tục mỗi chu kỳ điều khiển:

- ESP32 luôn cấu hình GPIO41 là **OUTPUT**, ghi mức `HIGH` khi đang có mẻ ấp
  chạy thật sự (`batchRunning_`), `LOW` khi không có mẻ nào.
- ATtiny **chỉ đọc** `PB0`, không bao giờ tự lái đường dây này trong bản này
  (tránh đụng độ 2 bên cùng kéo).
- Vì sao không cần timing phức tạp: đây không phải truyền dữ liệu nhiều bit,
  chỉ là 1 cờ boolean thay đổi rất hiếm khi (lúc bắt đầu/kết thúc mẻ ấp), nên
  một mức DC đơn giản là đủ và cực kỳ bền vững.

**Điểm mấu chốt của toàn bộ thiết kế**: đúng lúc ESP32 mất điện, `3.3V_ESP`
(nguồn của cả GPIO41 lẫn điện trở kéo lên R8) cũng biến mất — nghĩa là không
thể đọc "trạng thái đang ấp" ngay tại thời điểm mất điện được nữa. Vì vậy
ATtiny phải **liên tục ghi nhớ** giá trị đọc được gần nhất (biến RAM, không
mất khi ESP32 chết vì ATtiny chạy nguồn riêng) — giá trị nhớ được cuối cùng
chính là "trạng thái ngay trước lúc mất điện", dùng để quyết định có báo hay
không.

## 4. Nguyên lý thuật toán (ATtiny13A)

1. Ngủ **Power-down** giữa các lần đo, Watchdog đánh thức mỗi ~1 giây (tiết
   kiệm tối đa pin CR2032 — dòng ngủ chỉ còn vài µA).
2. Đọc ADC1 (3.3V_ESP):
   - **Còn điện** (≥ ngưỡng): đọc thêm PB0, cập nhật cờ nhớ
     `lastKnownBatchRunning`; nếu còi đang kêu thì **tắt ngay** (theo yêu cầu
     "tự tắt khi có điện lại"); reset bộ đếm debounce mất điện về 0.
   - **Nghi ngờ mất điện** (< ngưỡng): tăng bộ đếm debounce. Đạt **5 lần liên
     tiếp (~5 giây)** mới kết luận là mất điện thật (tránh báo giả do nhiễu
     điện áp thoáng qua) — lúc đó **chỉ bật còi nếu** `lastKnownBatchRuning`
     đang là `true` (đúng yêu cầu "chỉ báo khi đang có mẻ ấp chạy"); nếu
     không có mẻ nào đang chạy, im lặng hoàn toàn.

## 5. Ngưỡng ADC — cách tính lại nếu đổi điện trở

Ngưỡng hiện tại: `POWER_OK_ADC_THRESHOLD = 100` (thang 0-1023, tham chiếu là
chính VCC của ATtiny, tức điện áp pin CR2032 tại thời điểm đó).

- Lúc **còn điện**: điểm đo ≈ 1.98V. Vì tham chiếu ADC dùng VCC (pin CR2032,
  dao động 2.0V lúc pin yếu đến 3.3V lúc pin mới), số đếm ADC rơi vào khoảng
  **600–920** tuỳ tuổi pin. **Lưu ý**: pin càng già (VCC càng tụt), số đếm
  càng *tăng* (vì mẫu số VCC nhỏ đi) — không bao giờ trôi về phía ngưỡng.
- Lúc **mất điện**: điểm đo gần 0V bất kể VCC bao nhiêu → số đếm gần 0, luôn
  cách xa ngưỡng.
- Biên độ an toàn giữa 2 trạng thái là rất lớn (100 so với 600-920), nên
  không cần hiệu chỉnh định kỳ theo tuổi pin.

Nếu đổi giá trị R1/R2 trên mạch, công thức lại ngưỡng:
```
V_diem_do = 3.3V_ESP_day_du × R2/(R1+R2)
so_dem_toi_thieu_luc_du_dien ≈ V_diem_do / VCC_pin_yeu_nhat × 1023
```
rồi chọn ngưỡng mới bằng khoảng 15-20% giá trị đó để giữ biên độ an toàn.

## 6. Nạp code cho ATtiny13A

1. Cài board package **ATTinyCore** (Spence Konde) trong Arduino IDE
   (Boards Manager → tìm "ATTinyCore").
2. Chọn board **ATtiny13**, Clock **1.2 MHz (Internal, 9.6MHz Osc,
   CKDIV8)** — đúng fuse mặc định xuất xưởng, **không cần chỉnh fuse gì**.
3. Nối mạch nạp ISP (vd USBasp) vào đầu `J1`: MOSI=PB0, MISO=PB1, SCK=PB2,
   RESET=PB5, GND=GND. **J1 không cấp nguồn qua ISP** (chân VCC đã bỏ trong
   thiết kế) — bo mạch phải đã gắn sẵn pin CR2032 lúc nạp.
4. Nạp file `ATTINY13A_POWER_ALARM.ino` bằng **Sketch → Upload Using
   Programmer**.

**⚠️ TUYỆT ĐỐI KHÔNG đụng tới fuse RSTDISBL** (fuse giải phóng PB5 khỏi vai
trò RESET để dùng làm GPIO thường) — mạch chỉ dùng PB5/RESET với điện trở
kéo lên thông thường (R10), không cần fuse này. Nếu lỡ bật fuse đó, chỉ có
thể khôi phục bằng mạch nạp điện áp cao (HVSP), phức tạp hơn nhiều so với
ISP thường.

## 7. Còn để ngỏ / hướng mở rộng sau

- **PB3/ADC3 (đo 9V)**: mạch đã có sẵn, firmware chưa dùng. Có thể dùng để
  tự ATtiny biết "nguồn 9V cũng đã mất luôn" (khi đó còi không có gì để kêu
  dù có kích PB1), hoặc báo mức pin/nguồn 9V ngược về ESP32 qua bus khi còn
  sống.
- **Giao tiếp 2 chiều qua bus**: bản này chỉ 1 chiều (ESP32 → ATtiny). Nếu
  sau này cần ATtiny gửi dữ liệu về ESP32 (vd trạng thái pin CR2032, số lần
  đã từng báo mất điện...), cần thiết kế lại thành giao thức có bắt tay
  (turn-taking) để tránh 2 bên cùng lái một đường dây.
