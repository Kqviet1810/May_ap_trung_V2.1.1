# MÁY ẤP TRỨNG MAYAP - HƯỚNG DẪN VẬN HÀNH VÀ MÃ LỖI

**Phiên bản tài liệu:** v1.3  
**Firmware đối chiếu:** `MAYAP_INDUSTRIAL_v4_0_0` - nhánh `main`  
**Ngày cập nhật:** 30/09/2026  
**Nguồn chuẩn mã lỗi:** `MAYAP_INDUSTRIAL_v4_0_0/machine_control.h`

> Tài liệu này thay thế danh sách mã lỗi cũ chỉ cập nhật tới E502. Firmware hiện tại có **41 mã lỗi thực**, không tính `None = 0`.

---

## 1. Quy ước mức lỗi

| Mức | Ý nghĩa vận hành |
|---|---|
| **INFO** | Thông tin trạng thái, không phải sự cố. |
| **WARNING** | Cảnh báo sớm/chẩn đoán. Máy thường vẫn tiếp tục vận hành, nhưng cần kiểm tra nguyên nhân. |
| **STOP** | Lỗi nghiêm trọng. Tùy mã, hệ thống có thể cấm SSR, cắt contactor nhiệt, khóa đảo hoặc chặn chức năng liên quan. |
| **EMERGENCY** | Lỗi khẩn cấp. Ưu tiên an toàn, cắt đường gia nhiệt và thực hiện phản ứng bảo vệ ngay. |

### Nguyên tắc xử lý chung

1. Đọc **mã E...** trên HMI/Web trước khi thao tác.
2. Không cố bật lại heater/SSR hoặc đảo khi lỗi STOP/EMERGENCY chưa được xác định nguyên nhân.
3. Với lỗi cảm biến/nhiệt: kiểm tra cảm biến, dây, nguồn, RS485/I2C và nhiệt độ thực tế bằng thiết bị độc lập.
4. Với lỗi đảo: kiểm tra cơ khí, công tắc hành trình và dây điều khiển trước khi chạy lại.
5. Với lỗi EEPROM/RTC/NVS: không tự ý bắt đầu mẻ mới nếu hệ thống đang chặn an toàn.
6. ACK chỉ xác nhận người vận hành đã biết lỗi; ACK **không được coi là sửa lỗi**.
7. Nếu lỗi tái xuất hiện sau khi đã khắc phục, dừng máy và kiểm tra phần cứng tương ứng.

---

# 2. Danh sách 41 mã lỗi hiện hành

## Nhóm E101-E104 - Cảm biến nhiệt

| Mã | Tên firmware | Mức | Ý nghĩa / phản ứng hệ thống | Xử lý vận hành |
|---|---|---|---|---|
| **E101** | `SensorLost` | STOP | Mất dữ liệu cảm biến. Cấm SSR, cắt contactor nhiệt; duy trì tuần hoàn. | Kiểm tra nguồn cảm biến, dây, đầu nối, bus giao tiếp. Không chạy heater khi chưa có nhiệt độ tin cậy. |
| **E102** | `SensorInvalid` | STOP | Dữ liệu cảm biến không hợp lệ. Cấm SSR và cắt contactor nhiệt. | Kiểm tra giá trị đọc, dây tín hiệu, cấu hình cảm biến và nhiễu điện. |
| **E103** | `SensorSuspect` | STOP | Dữ liệu cảm biến đáng ngờ dù chưa mất hoàn toàn. Cấm gia nhiệt. | Đối chiếu nhiệt độ bằng nhiệt kế độc lập; kiểm tra cảm biến và đường truyền. |
| **E104** | `SensorFrozen` | STOP | Giá trị PV đứng bất thường trong khi heater đã có ON-time đáng kể. Lỗi có tính giữ; cắt gia nhiệt và hỗ trợ thông gió/tuần hoàn. | Kiểm tra cảm biến bị treo, kẹt giá trị, nguồn/bus; chỉ chạy lại sau khi xác nhận PV thay đổi bình thường. |

## Nhóm E110-E115 - Nhiệt độ và hệ thống gia nhiệt

| Mã | Tên firmware | Mức | Ý nghĩa / phản ứng hệ thống | Xử lý vận hành |
|---|---|---|---|---|
| **E110** | `LowTemperature` | WARNING | Nhiệt độ thấp hơn ngưỡng cảnh báo. | Kiểm tra heater, SSR, contactor, cửa máy, quạt và tải nhiệt. |
| **E111** | `HighTemperature` | STOP | Nhiệt độ cao. Cấm SSR, cắt contactor nhiệt, dừng đảo và bật thông gió/tuần hoàn bảo vệ. | Kiểm tra SSR dính, contactor, cảm biến, luồng khí; xác nhận nhiệt giảm an toàn trước khi chạy lại. |
| **E112** | `EmergencyTemperature` | EMERGENCY | Quá nhiệt khẩn cấp. Cắt gia nhiệt ngay, dừng đảo, bật quạt bảo vệ và cảnh báo khẩn. | Cắt nguồn gia nhiệt nếu cần, kiểm tra SSR/contactor và nhiệt độ thực tế. Không reset liên tục khi nhiệt còn cao. |
| **E113** | `TemperatureRateExceeded` | WARNING | Tốc độ tăng nhiệt bất thường. | Theo dõi xu hướng nhiệt; kiểm tra PID, SSR, cảm biến và công suất heater. |
| **E114** | `TemperatureUnstable` | WARNING | Nhiệt dao động bất thường. | Kiểm tra PID, vị trí cảm biến, tuần hoàn gió, relay/SSR và nhiễu cảm biến. |
| **E115** | `HeaterNotHeating` | STOP | Có yêu cầu gia nhiệt nhưng nhiệt không tăng đủ. Lỗi có tính giữ; cắt SSR/contactor và bật hỗ trợ bảo vệ. | Kiểm tra heater, SSR, contactor, CB/cầu chì, dây công suất và nguồn AC. |

## Nhóm E120-E121 - Độ ẩm

| Mã | Tên firmware | Mức | Ý nghĩa / phản ứng hệ thống | Xử lý vận hành |
|---|---|---|---|---|
| **E120** | `HumidityLow` | WARNING | Độ ẩm thấp hơn ngưỡng cài đặt. | Kiểm tra nước, bộ tạo ẩm, cảm biến ẩm và thông gió. |
| **E121** | `HumidityHigh` | WARNING | Độ ẩm cao hơn ngưỡng cài đặt. | Kiểm tra bộ tạo ẩm, thông gió, nước đọng và cảm biến ẩm. |

## Nhóm E130, E132-E138 - Mẻ ấp / AUTO / phục hồi

> **E131 không còn sử dụng.** Quạt tuần hoàn hiện được firmware ép chạy bắt buộc trong mẻ nên mã cũ liên quan công tắc quạt đã được loại bỏ.

| Mã | Tên firmware | Mức | Ý nghĩa / phản ứng hệ thống | Xử lý vận hành |
|---|---|---|---|---|
| **E130** | `HeaterSwitchOffDuringBatch` | STOP | Công tắc HEATER bị tắt trong khi mẻ đang chạy. Cấm SSR và cắt contactor nhiệt. | Kiểm tra công tắc HEATER và chỉ bật lại khi bảo đảm an toàn. |
| **E132** | `ResumeRequiresAuto` | WARNING | Máy cần AUTO để tiếp tục quy trình phục hồi mẻ. | Gạt AUTO đúng trạng thái rồi thực hiện xác nhận phục hồi theo HMI. |
| **E133** | `AutoModeOffDuringBatch` | STOP | AUTO bị tắt trong mẻ. Firmware vẫn giữ điều khiển nhiệt; mã dùng để cảnh báo mạnh người vận hành. | Kiểm tra lý do AUTO bị tắt; đưa về AUTO nếu muốn máy tiếp tục chu trình tự động. |
| **E134** | `AutoTurningDisabledDuringBatch` | STOP | Chức năng đảo tự động bị vô hiệu khi mẻ đang chạy; khóa đảo tự động. | Kiểm tra cài đặt đảo, công tắc và trạng thái cơ khí trước khi bật lại. |
| **E135** | `ResumeConfirmationPending` | WARNING | Sau mất điện máy chờ xác nhận “ấp lại mẻ cũ” quá lâu. | Trên HMI chọn tiếp tục hoặc hủy mẻ theo tình trạng thực tế. |
| **E136** | `BatchOverdue` | WARNING | Mẻ đã vượt số ngày ấp dự kiến. | Kiểm tra tuổi mẻ và tình trạng trứng; quyết định kết thúc hay tiếp tục theo quy trình. |
| **E137** | `ResumeRtcWaitTooLong` | WARNING | Phục hồi mẻ bị chờ lâu do RTC chưa hợp lệ. | Kiểm tra DS3231, pin RTC, bus I2C và đồng bộ thời gian/NTP. |
| **E138** | `BatchOverdueConfirmationPending` | STOP | Mẻ quá hạn đang chờ người dùng xác nhận. Firmware có thể tự dừng mẻ sau thời gian grace cấu hình nếu không có thao tác. | Xác nhận tiếp tục ủ ấm hoặc chủ động kết thúc mẻ. |

## Nhóm E201-E205 - Hệ thống đảo trứng

| Mã | Tên firmware | Mức | Ý nghĩa / phản ứng hệ thống | Xử lý vận hành |
|---|---|---|---|---|
| **E201** | `TurnLimitConflict` | STOP | Hai công tắc hành trình có trạng thái xung đột. Lỗi giữ và khóa đảo. | Kiểm tra 2 limit, dây ACTIVE-LOW, gá cơ khí và nhiễu đầu vào. |
| **E202** | `TurnTimeout` | STOP | Motor đảo chạy quá thời gian mà chưa tới hành trình. Lỗi giữ và khóa đảo. | Kiểm tra motor, relay/contactor, kẹt cơ khí và công tắc hành trình. |
| **E203** | `TurnLimitStuck` | STOP | Công tắc hành trình bị kẹt/không nhả đúng logic. Lỗi giữ và khóa đảo. | Kiểm tra công tắc, cơ cấu tác động và dây tín hiệu. |
| **E204** | `TurnCommandConflict` | STOP | Xuất hiện lệnh đảo trái/phải xung đột. Lỗi giữ và khóa đảo. | Kiểm tra nút tay, input, relay và logic điều khiển ngoài. |
| **E205** | `TurnMechanicalCheckRequired` | STOP | Lỗi đảo lặp lại vượt ngưỡng; hệ thống yêu cầu kiểm tra cơ khí trước khi cho đảo lại. | Vào Test Mode, kiểm tra cả hai chiều và cả hai công tắc hành trình đạt Success rồi mới đưa vào vận hành. |

## Nhóm E301-E306, E313-E315 - Lưu trữ / reset / output / RTC

| Mã | Tên firmware | Mức | Ý nghĩa / phản ứng hệ thống | Xử lý vận hành |
|---|---|---|---|---|
| **E301** | `StorageUnavailable` | STOP | EEPROM ngoài không khả dụng. Cấm SSR để tránh vận hành với dữ liệu cấu hình/trạng thái không tin cậy. | Kiểm tra AT24Cxx, nguồn, SDA/SCL, địa chỉ I2C và hàn nối. |
| **E302** | `StorageDegraded` | WARNING | EEPROM hoạt động nhưng có dấu hiệu suy giảm/retry. | Kiểm tra bus I2C, nguồn, nhiễu; theo dõi tần suất lỗi. |
| **E303** | `AbnormalReset` | STOP | ESP32 vừa reset bất thường (WDT, panic...). Hệ thống yêu cầu xác nhận an toàn. | Xem nhật ký reset, kiểm tra nguồn, watchdog và nguyên nhân treo trước khi tiếp tục. |
| **E304** | `OutputConflict` | EMERGENCY | Trạng thái output có xung đột nguy hiểm. Cấm SSR và khóa đảo. | Dừng vận hành; kiểm tra GPIO, relay/contactor, short/chạm dây và logic output. |
| **E305** | `RelayRateExceeded` | WARNING | Relay đóng/cắt quá nhanh hoặc quá nhiều. Lỗi có tính giữ để tránh bỏ qua hiện tượng chatter. | Kiểm tra hysteresis, input nhiễu, relay và thuật toán điều khiển tải. |
| **E306** | `RtcFailure` | WARNING | RTC không hợp lệ/offline/oscillator stop. | Kiểm tra DS3231, pin RTC, SDA/SCL; cài/đồng bộ lại thời gian. |
| **E313** | `BatchStateClearPending` | STOP | Đã yêu cầu dừng/hủy nhưng trạng thái `wasRunning=0` chưa được EEPROM xác nhận. Cấm SSR. | Không bắt đầu/phục hồi mẻ mới; kiểm tra EEPROM và cho hệ thống hoàn tất ghi trạng thái. |
| **E314** | `SafetyJournalUnavailable` | STOP | NVS safety journal không khả dụng; cấm SSR và chặn thao tác nguy hiểm liên quan phục hồi. | Kiểm tra flash/NVS, firmware và nguyên nhân lỗi ghi; không bypass. |
| **E315** | `BatchLogUnavailable` | WARNING | Nhật ký mẻ không ghi được. Điều khiển nhiệt/đảo vẫn ưu tiên tiếp tục. | Kiểm tra bộ nhớ/log; sao lưu nhật ký nếu cần và theo dõi tái diễn. |

## Nhóm E401-E404 - Giám sát sức khỏe hệ thống

| Mã | Tên firmware | Mức | Ý nghĩa / phản ứng hệ thống | Xử lý vận hành |
|---|---|---|---|---|
| **E401** | `HeapLow` | WARNING | RAM heap xuống thấp bất thường. | Theo dõi runtime, log, kết nối mạng và khả năng rò bộ nhớ. |
| **E402** | `HeapCritical` | WARNING | Heap xuống mức nguy cấp; firmware có thể yêu cầu restart chủ động để tự phục hồi. | Kiểm tra log sau restart; nếu lặp lại cần audit memory/network task. |
| **E403** | `TemperatureTrendWarning` | WARNING | Xu hướng nhiệt cho thấy nguy cơ lệch dù chưa chạm ngưỡng lỗi cứng. | Kiểm tra tải nhiệt, PID, cảm biến và thông gió trước khi lỗi nặng hơn. |
| **E404** | `StorageRetryTrend` | WARNING | Số lần retry bộ nhớ tăng bất thường. | Kiểm tra EEPROM/I2C, nguồn và nhiễu; xử lý trước khi thành E301/E302. |

## Nhóm E501-E503 - Bộ cảnh báo mất điện ATtiny13A

| Mã | Tên firmware | Mức | Ý nghĩa / phản ứng hệ thống | Xử lý vận hành |
|---|---|---|---|---|
| **E501** | `AttinyBusUnresponsive` | WARNING | ESP32 không nhận ACK/status từ ATtiny. Không tác động trực tiếp tới điều khiển nhiệt/đảo. | Kiểm tra nguồn ATtiny, pin dự phòng, dây BUS 1-wire và mức 3.3 V. |
| **E502** | `SirenBatteryLow` | WARNING | ATtiny báo nguồn/pin cấp còi thấp. | Kiểm tra và thay/sạc nguồn dự phòng của còi; thử còi sau xử lý. |
| **E503** | `AttinyStateUnsynced` | WARNING | Link ATtiny còn sống nhưng trạng thái mẻ giữa ESP32 và ATtiny không đồng bộ. | Kiểm tra BUS, đồng bộ trạng thái mẻ và thử start/stop mẻ để xác nhận ATtiny nhận đúng. |

---

# 3. Phản ứng ưu tiên khi có lỗi nhiệt

1. **E112 - EmergencyTemperature:** ưu tiên cao nhất; cắt gia nhiệt ngay.
2. **E111 - HighTemperature:** cắt SSR + contactor và tăng thông gió/tuần hoàn.
3. **E101/E102/E103/E104:** khi không thể tin cậy cảm biến, hệ thống không được phép tiếp tục gia nhiệt.
4. **E115:** có yêu cầu nhiệt nhưng buồng không tăng nhiệt; kiểm tra đường công suất trước khi reset.
5. Không dùng ACK để ép máy chạy khi nguyên nhân vật lý chưa được xử lý.

---

# 4. Phản ứng ưu tiên khi có lỗi đảo

- E201-E205 đều phải được coi là lỗi cơ khí/đầu vào cần kiểm tra thực tế.
- Không đấu tắt limit để vượt lỗi.
- Sau lỗi lặp lại E205, phải kiểm tra cả hai chiều trong **Test Mode**.
- Chỉ đưa về AUTO sau khi motor, relay và 2 công tắc hành trình hoạt động đúng.

---

# 5. Mất điện và phục hồi mẻ

- ESP32 lưu trạng thái mẻ và dùng RTC để tính thời gian mất điện/phục hồi.
- ATtiny13A là lớp cảnh báo mất điện độc lập, không thay thế logic an toàn chính của ESP32.
- Khi hiện yêu cầu **“Tiếp tục mẻ?”**, người vận hành phải chọn theo tình trạng thực tế của mẻ.
- Nếu E137 xuất hiện, xử lý RTC trước khi tin cậy thời gian phục hồi.
- Nếu E503 xuất hiện, kiểm tra trạng thái đồng bộ ESP32 - ATtiny trước khi bàn giao máy.

---

# 6. Kiểm tra trước khi bắt đầu mẻ

- Cảm biến nhiệt đọc ổn định, không có E101-E104.
- RTC đúng ngày giờ, không có E306/E137.
- EEPROM/NVS không có E301/E313/E314.
- Heater, SSR, contactor và quạt chạy đúng Test Mode.
- Hai công tắc hành trình đảo hoạt động đúng, không có E201-E205.
- AUTO bật theo cấu hình vận hành.
- Bộ ATtiny/còi dự phòng không có E501-E503 hoặc đã được xử lý.
- Không có fault STOP/EMERGENCY chưa xác định nguyên nhân.

---

# 7. Ghi chú kỹ thuật cho bảo trì

Firmware khai báo:

```cpp
constexpr uint8_t FAULT_CODE_REAL_COUNT = 41U;
```

`FaultManager` hiện có `MAX_FAULTS = 48`, kèm `static_assert` để tránh trường hợp số mã lỗi vượt sức chứa bảng. Trước đây từng có tình trạng số fault thực lớn hơn số slot, có thể làm một số fault bị ghi đè; kiến trúc v4.0.0 đã bổ sung headroom và kiểm tra compile-time.

Khi thêm mã lỗi mới trong firmware phải cập nhật đồng thời:

1. `enum class FaultCode`.
2. `FAULT_CODE_REAL_COUNT`.
3. `faultDescriptor()`.
4. HMI/Web/Cloud mapping nếu có.
5. Tài liệu này và bản PDF phát hành cho người vận hành.

---

**Kết thúc tài liệu - v1.3 / 41 mã lỗi / firmware v4.0.0**
