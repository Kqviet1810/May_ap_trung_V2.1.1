# Báo mất điện độc lập qua ATtiny13A

Bổ sung sau audit v3.7.1: khi ESP32 mất điện, còi do ESP32 điều khiển
(`PIN_OUT_SIREN`, GPIO47) cũng mất điện theo — vô dụng đúng lúc cần báo nhất.
Giải pháp: chuyển việc báo mất điện sang một chip **ATtiny13A-PU** chạy hoàn
toàn độc lập, có nguồn riêng backup bằng pin cúc áo CR2032, nên vẫn sống dù
ESP32 và cả nguồn 9V chính đều mất.

Sơ đồ gốc: `Schematic_ESP32_MAT_HMI_V1.pdf` (do người dùng cung cấp,
2026-09-16), linh kiện `U6`.

**Bản thiết kế này là bản sửa lớn** so với bản đầu tiên (poll định kỳ 1
giây qua Watchdog + đọc mức tĩnh trên bus) — theo yêu cầu tường minh của
người dùng, ưu tiên tuyệt đối là **tiết kiệm pin CR2032**, nên ATtiny **ngủ
Power-down toàn phần**, chỉ thức khi có **ngắt thay đổi mức tín hiệu (Pin
Change Interrupt)** trên 1 trong 3 chân theo dõi, không còn thức định kỳ nào
cả. Giao tiếp giữa 2 chip là giao thức **1 dây, đếm xung, có bắt tay
ACK/thử lại**, không phải mức tĩnh như bản đầu.

## 1. Ba khối mạch liên quan

**Nguồn riêng cho ATtiny** — net `3.3V` (khác `3.3V_ESP` của ESP32), có pin
CR2032 (B2) + tụ lọc C2/C4. Chỉ cấp cho ATtiny13A, không liên quan gì đến
ESP32 hay nguồn 9V.

**Mạch chia áp đo mức tín hiệu** (đưa về 2 chân số của ATtiny — **không**
dùng ADC trong bản này, chỉ đọc mức cao/thấp số vì yêu cầu chỉ là "phát hiện
mất điện", không cần đo chính xác điện áp):
- `3.3V_ESP` → cầu phân áp R1/R2 → `PB2`: lúc đầy đủ điện, điểm đo đủ cao để
  ATtiny đọc mức **HIGH**; lúc mất điện, điểm đo về gần 0V, đọc mức **LOW**.
- `9V` (nguồn nuôi còi) → cầu phân áp R4/R5/R6 → `PB3`: điểm chia đã được
  thiết kế sao cho ngưỡng số HIGH/LOW của ATtiny rơi vào khoảng nguồn 9V thật
  còn **≈ 7V** (theo yêu cầu người dùng — không cần chính xác tuyệt đối, chỉ
  cần phát hiện được lúc pin 9V yếu dần).

Điện trở cỡ MΩ → dòng tiêu thụ mỗi nhánh dưới 1µA, không đáng kể so với tuổi
thọ pin CR2032.

**Còi báo** — `PB1` qua R7(100k) kích cực B của Q2 (NPN), Q2 đóng/cắt còi
(U21) dùng nguồn **9V** (không phải 3.3V) — nên còi vẫn kêu được dù ESP32 đã
chết, miễn nguồn 9V/pin dự phòng còn.

## 2. Bảng chân ATtiny13A (DIP8)

| Pin vật lý | Tên | Vai trò trong mạch |
|---|---|---|
| 1 | PB5 / RESET | Kéo lên 10k (R10) — giữ nguyên vai trò RESET, **không** dùng làm GPIO |
| 2 | PB3 | Đọc mức số nguồn 9V (cầu phân áp R4/R5/R6) |
| 3 | PB4 | Nối thẳng lên 3.3V — không dùng, tránh thả nổi |
| 4 | GND | — |
| 5 | PB0 / MOSI | Bus 1 dây 2 chiều với ESP32 (GPIO41) |
| 6 | PB1 / MISO | Điều khiển còi (qua Q2) |
| 7 | PB2 / SCK | Đọc mức số 3.3V_ESP (cầu phân áp R1/R2) |
| 8 | VCC | Nguồn `3.3V` riêng, backup CR2032 |

Chân ISP (J1, đầu nạp 2x3 chuẩn AVR: MOSI/MISO/SCK/RESET/GND) dùng chung với
PB0/PB1/PB2/PB5 — bình thường không xung đột vì ISP chỉ hoạt động lúc nạp
code, không chạy song song với firmware ứng dụng.

**Vì sao không dùng Bộ so sánh tương tự (Analog Comparator) để đánh thức
CPU?** Bộ so sánh của ATtiny13A chỉ nối cứng với đúng 2 chân `PB0`/`PB1`
trong khi 2 cầu phân áp lại đi vào `PB2`/`PB3` theo sơ đồ có sẵn — không thể
dùng được mà không sửa mạch. Giải pháp: dùng **Pin Change Interrupt**, hoạt
động được trên **bất kỳ chân nào** trong PB0-PB5, không cần sửa mạch, và vẫn
đánh thức được CPU ngay khi mức tín hiệu đổi (biên độ đổi giữa 2 trạng thái
rất lớn nên luôn vượt ngưỡng Schmitt-trigger số của chân, không cần độ chính
xác của bộ so sánh tương tự).

## 3. Giao thức bus 1 dây (PB0 ↔ ESP32 GPIO41)

Bus kiểu **"hở máng" (open-drain)**: cả 2 bên **chỉ được** kéo `LOW` hoặc
thả nổi (`INPUT`) — **không bao giờ** chủ động ghi `HIGH` — đường dây được
giữ mức nghỉ (`HIGH`) nhờ điện trở kéo lên R8 (10k) phía ESP32
(`3.3V_ESP`). Tránh đụng độ nếu 2 bên vô tình "nói" cùng lúc.

**Mã hoá: đếm xung.** Một bản tin = N xung `LOW` liên tiếp, mỗi xung dài
`PULSE_MS = 30ms`, cách nhau cùng khoảng đó, kết thúc bằng khoảng lặng
(`HIGH`) tối thiểu `END_GAP_MS = 150ms`. N chính là mã lệnh. Xung ngắn hơn
`MIN_PULSE_MS = 15ms` bị coi là nhiễu, không tính. Bên nhận, sau khi kết
luận bản tin đã xong (đủ lâu im lặng), gửi lại đúng **1 xung** làm ACK trong
vòng `ACK_TIMEOUT_MS = 200ms`; nếu bên gửi không thấy ACK, tự động gửi lại
tối đa `MAX_RETRY = 3` lần.

**Vì sao chọn đếm xung thay vì UART-kiểu-baudrate:** ESP32 dùng thạch anh
(độ chính xác cao), ATtiny13A dùng dao động RC nội bộ (sai số nhà sản xuất
công bố tới ±10%). Khung truyền kiểu UART cần 2 bên đồng bộ tốc độ bit rất
sát nhau mới giải mã đúng; đếm xung với biên độ dung sai lớn (30ms/15ms) chịu
được sai lệch đồng hồ đó dễ dàng mà không cần bất kỳ cơ chế đồng bộ nào.

**Bảng mã bản tin:**

| Mã | Chiều | Ý nghĩa |
|---|---|---|
| 1 | ESP32 → ATtiny | Bắt đầu 1 mẻ ấp (`dangCoMe = true`) |
| 2 | ESP32 → ATtiny | Kết thúc mẻ ấp (`dangCoMe = false`, tắt còi báo-mất-điện nếu đang bật) |
| 3 | ESP32 → ATtiny | **Bật còi** — lệnh khẩn cấp trực tiếp (chỉ dùng khi ESP32 đang báo quá nhiệt khẩn cấp), **luôn ưu tiên** bất kể `dangCoMe` |
| 4 | ESP32 → ATtiny | **Tắt còi** — huỷ lệnh khẩn cấp mã 3 |
| 5 | ESP32 → ATtiny | Ping kiểm tra ATtiny còn sống (gửi lúc bắt đầu mẻ, và định kỳ mỗi 6 giờ trong lúc mẻ đang chạy) — ATtiny chỉ cần ACK |
| 6 | ATtiny → ESP32 | Báo nguồn 9V đã tụt dưới ngưỡng (~7V) — cần ACK, ATtiny tự thử lại nếu chưa được ACK |

Không còn mã 7 trở lên (giới hạn hiện tại `MSG_MAX_CODE = 6`).

**Cơ chế bật/tắt còi trên ATtiny (độc lập với dữ liệu từ ESP32, lưu trong
RAM, không mất khi ESP32 mất điện vì ATtiny chạy nguồn riêng):**
- `coiKhauCap`: bật/tắt trực tiếp theo mã 3/4 — **luôn ưu tiên tuyệt đối**,
  mirror y hệt chu kỳ tắt-tạm/kêu-lại của còi khẩn cấp hiện có bên ESP32
  (ESP32 tự lặp lại việc gửi mã 3/4 đúng theo chu kỳ đó, ATtiny không cần tự
  biết chu kỳ là gì, chỉ cần vâng lệnh).
- `coiMatDien`: tự động bật khi ATtiny phát hiện `3.3V_ESP` mất **và**
  `dangCoMe == true`; **tự động tắt** ngay khi phát hiện có điện lại (không
  cần lệnh gì từ ESP32, vì lúc đó ESP32 vốn đang mất điện, không thể gửi
  lệnh).
- Còi thực tế kêu khi `coiKhauCap OR coiMatDien` (2 cờ độc lập, không loại
  trừ nhau).

## 4. Nguyên lý thuật toán (ATtiny13A) — `ATTINY13A_POWER_ALARM.ino`

1. **Ngủ Power-down toàn phần** làm trạng thái mặc định tuyệt đối — dòng
   tiêu thụ chỉ còn vài µA. **Không còn Watchdog đánh thức định kỳ** như bản
   cũ — cơ chế thức duy nhất là **Pin Change Interrupt** trên `PB0` (bus),
   `PB2` (3.3V_ESP), `PB3` (9V). ISR để trống hoàn toàn, chỉ dùng để thoát
   `sleep_mode()` — mọi xử lý thật sự nằm ở vòng lặp chính sau khi thức dậy.
2. Sau mỗi lần thức, vòng lặp chính kiểm tra theo thứ tự:
   - **Bus đang mức LOW?** → có bản tin ESP32 đang gửi tới → nhận đủ xung
     (đến khi im lặng ≥150ms), giải mã, cập nhật `dangCoMe`/`coiKhauCap` theo
     mã nhận được, gửi lại 1 xung ACK, cập nhật còi.
   - **3.3V_ESP đổi trạng thái?** → đợi 50ms rồi đọc lại (chống nhiễu/gợn
     sóng thoáng qua) — nếu vẫn đổi thật, cập nhật `coiMatDien` theo mục 3.
   - **9V đổi trạng thái?** → đợi 50ms rồi đọc lại — nếu tụt xuống thật, gửi
     mã 6 (có ACK + tự thử lại tối đa 3 lần); nếu phục hồi, **không cần gửi
     gì** (xem mục 5).
3. Quay lại ngủ Power-down.

## 5. Vì sao không cần bản tin "9V đã phục hồi"

Giao thức chỉ có 1 chiều báo "vừa tụt xuống thấp" (mã 6), không có mã báo
"đã phục hồi". Thay vào đó, bên ESP32 (`machine_control.h`,
`updateAttinyLink()`) tự cho cảnh báo `Attiny9VLow` **tự hết sau 24 giờ**
kể từ lần nhận mã 6 gần nhất (nếu có mã 6 mới trong lúc đó, thời hạn được
làm mới). Cách này tránh phải thêm 1 mã bản tin mới chỉ để báo "hết", đổi
lại là cảnh báo có thể hiển thị hơi lâu hơn thực tế tối đa 24 giờ sau khi
pin 9V đã được thay — chấp nhận được vì đây là cảnh báo mức Warning (không
chặn vận hành).

## 6. Đánh giá rủi ro / các đánh đổi đã chấp nhận

- **`mayapAttinyBusSend()` phía ESP32 là hàm BLOCKING**, tối đa xấp xỉ
  `MAX_RETRY × (thời gian gửi xung + ACK_TIMEOUT_MS)` ≈ **dưới 2 giây** ở
  trường hợp xấu nhất (ATtiny không phản hồi cả 3 lần thử) — được gọi trực
  tiếp trong cùng chu kỳ điều khiển đang lo cả nhiệt độ lẫn đảo trứng. Chấp
  nhận được vì các điểm gọi đều **rất hiếm khi xảy ra** (bắt đầu/kết thúc mẻ
  mỗi mẻ 1 lần, ping mỗi 6 giờ, đổi trạng thái còi khẩn cấp chỉ khi thật sự
  có sự cố nhiệt độ khẩn cấp) — không nằm trong đường điều khiển nhiệt/đảo
  chạy liên tục mỗi vòng lặp.
- **Ngưỡng số (digital) thay vì đo điện áp chính xác**: theo đúng yêu cầu
  người dùng ("miễn là phát hiện mất là được"), ngưỡng 9V (~7V) sẽ trôi nhẹ
  theo tuổi thọ pin CR2032 nuôi ATtiny (ngưỡng Schmitt-trigger của chân số
  tỉ lệ với VCC của chính ATtiny) — sai số ước tính trong khoảng ±1V quanh
  mốc 7V khi pin CR2032 xuống gần cuối vòng đời. Chấp nhận được vì mục đích
  là cảnh báo sớm để thay pin, không phải đo lường chính xác.
- **Đã bỏ tính năng ATtiny tự báo mức pin CR2032 của chính nó**: đã nghiên
  cứu và xác nhận **ATtiny13A không hỗ trợ đo VCC của chính mình qua mẹo
  bandgap nội bộ** (khác với các AVR/ATtiny đời khác — mux ADC của
  ATtiny13A không có kênh bandgap). Muốn làm được cần sửa mạch (thêm 1 cầu
  phân áp trên PB4). Người dùng đã chọn **bỏ tính năng, không sửa mạch**.
  Hệ quả: không có cách nào biết pin CR2032 nuôi chính ATtiny đang yếu dần,
  **ngoại trừ gián tiếp qua việc ping định kỳ (mã 5) bắt đầu không được ACK
  nữa** — lúc đó `FaultCode::AttinyBusUnresponsive` sẽ báo, nhưng đây là dấu
  hiệu ATtiny **đã chết hẳn**, không phải cảnh báo sớm. Đây là điểm-lỗi-đơn
  (single point of failure) còn tồn tại có chủ đích theo quyết định của
  người dùng.
- **Trường hợp bus bị kẹt mức LOW thật sự (lỗi phần cứng, đứt/chạm dây)**:
  `receiveMessage()` có giới hạn tự thoát sau khoảng 9 "xung" giả liên tiếp
  (~1 giây) để không treo vô hạn, nhưng sẽ không tự phát hiện được lỗi này
  cho tới khi có sự kiện thật tiếp theo trên 1 trong 3 chân theo dõi (vì
  ATtiny vẫn ngủ lại bình thường sau khi thoát) — chấp nhận được vì bus lỗi
  cứng là sự cố phần cứng hiếm, và phía ESP32 vẫn phát hiện được qua
  `AttinyBusUnresponsive` ở lần gửi kế tiếp (ping/đổi trạng thái mẻ/còi).
- **Rủi ro tranh chấp bus lý thuyết**: nếu đúng lúc ESP32 chủ động gửi bản
  tin thì ATtiny cũng vừa quyết định gửi mã 6 (báo 9V yếu), 2 bên có thể
  "nói" chồng lên nhau. Không có cơ chế phân xử tranh chấp (arbitration)
  tường minh — nhưng tự phục hồi được vì cả 2 phía đều có timeout + thử lại
  độc lập, và các sự kiện này cực hiếm khi trùng thời điểm (cửa sổ giao nhau
  chỉ vài trăm ms trên hàng giờ hoạt động).
- **Đã kiểm thử biên dịch thật** bằng `avr-gcc -mmcu=attiny13a` trong môi
  trường CI/dev: biên dịch sạch, không cảnh báo (`-Wall -Wextra`), kích
  thước chương trình **592/1024 byte flash (57.8%)**, **0 byte RAM tĩnh**
  (biến cục bộ nằm trên stack, độ sâu gọi hàm rất nông) — dư địa rộng rãi so
  với giới hạn phần cứng.

## 7. Nạp code cho ATtiny13A

1. Cài board package **ATTinyCore** (Spence Konde) trong Arduino IDE
   (Boards Manager → tìm "ATTinyCore").
2. Chọn board **ATtiny13**, Clock **1.2 MHz (Internal, 9.6MHz Osc,
   CKDIV8)** — đúng fuse mặc định xuất xưởng, **không cần chỉnh fuse gì**.
   Giữ nguyên fuse BOD (Brown-out Detection) ở trạng thái **DISABLED** mặc
   định của chip mới — bật BOD sẽ tốn thêm dòng rò rỉ đáng kể ngay cả lúc
   ngủ Power-down (ATtiny13A không có tính năng tự tắt BOD lúc ngủ như các
   AVR đời mới hơn), ảnh hưởng trực tiếp tới tuổi thọ pin CR2032.
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
