# MAYAP v3.8.1 - Commissioning & 72h soak checklist

Mục tiêu của checklist này là ưu tiên **an toàn, độ ổn định, khả năng tự phục hồi và khả năng chẩn đoán** cho quy mô vài máy đến vài chục máy. Không dùng checklist này để tuyên bố chứng nhận ISO/IEC/ETSI.

## 1. Trước khi cấp điện

- [ ] Xác nhận ESP32-S3-WROOM-1U-N8, flash thực 8 MB, không PSRAM.
- [ ] Xác nhận heater đi qua đúng chuỗi: cầu chì nhiệt -> thermostat độc lập -> contactor -> SSR -> heater.
- [ ] Thermostat có cảm biến riêng và cắt trực tiếp cuộn contactor, không qua ESP32.
- [ ] Kiểm tra dây PE/tiếp địa, cầu chì/CB, tiết diện dây heater và điểm siết terminal.
- [ ] Kiểm tra D1/D2 SSR, D14 heater master, D12 vent, D21 circulation ở trạng thái SAFE khi ESP32 chưa boot.
- [ ] Kiểm tra limit LEFT/RIGHT không đồng thời active trong trạng thái cơ khí bình thường.

## 2. Firmware / identity

- [ ] Firmware hiển thị version 3.8.1.
- [ ] Device ID có dạng `MAP-XXXXXXXXXXXX` và khác các máy khác.
- [ ] Sau lần boot đầu, `device-key` tồn tại bền vững qua reboot/nạp firmware thường.
- [ ] Không xóa NVS sau khi thiết bị đã đăng ký Cloud trừ khi có quy trình reset toàn bộ identity.
- [ ] HMI không kẹt `DANG DONG BO` vô hạn khi provisioning lỗi; phải chuyển thành trạng thái chẩn đoán phù hợp.

## 3. Provisioning nhiều máy

Thực hiện với ít nhất 2 ESP32 khác nhau nhưng cùng credential HiveMQ:

- [ ] Máy 1 tự `/api/device/register` thành công và nhận PIN.
- [ ] Máy 2 tự `/api/device/register` thành công và nhận PIN mà không cần INSERT D1 thủ công.
- [ ] Hai máy có topic MQTT riêng theo Device ID và không ghi nhầm trạng thái sang nhau.
- [ ] `device_inventory.enabled=0` đối với một ID thử nghiệm phải chặn ID đó dù auto provisioning đang bật.
- [ ] `REQUIRE_DEVICE_INVENTORY=1` phải khôi phục chế độ factory allowlist nghiêm ngặt.
- [ ] Rate-limit đăng ký mới không ảnh hưởng máy đã đăng ký reconnect/reboot.

## 4. PIN / browser session

- [ ] Reset PIN tại HMI trên máy đã đăng ký tạo PIN mới và revoke browser session cũ theo Worker.
- [ ] Reset PIN trên máy chưa từng register thành công phải self-heal: provisioning trước rồi trả PIN mới.
- [ ] Sai PIN liên tiếp bị rate-limit theo policy Worker.
- [ ] Browser session hết hạn/revoke không làm mất điều khiển cục bộ tại HMI.

## 5. Network resilience

Mỗi thử nghiệm dưới đây thực hiện khi heater đang điều khiển ổn định và theo dõi nhiệt độ liên tục.

- [ ] Tắt router 10 phút: PID/control/turning vẫn chạy local.
- [ ] Bật router lại: Wi-Fi, MQTT và Cloud tự reconnect, không cần reboot.
- [ ] Chặn MQTT nhưng giữ Internet: Cloud HTTPS vẫn hoạt động độc lập.
- [ ] Chặn Cloudflare nhưng giữ MQTT: web realtime vẫn hoạt động độc lập.
- [ ] DNS lỗi/tạm thời không resolve: controlTask không trễ/treo.
- [ ] RSSI yếu: phát cảnh báo nhưng không tạo vòng reconnect quá nhanh.

## 6. Sensor / heater fault injection

- [ ] Rút cảm biến: SSR OFF và contactor nhả theo policy safety.
- [ ] Chập/giá trị invalid: SSR OFF và contactor nhả.
- [ ] Sensor Frozen: chỉ trip khi PV đứng lâu **và** heater đã tích lũy ON-time đủ; khi trip phải cắt SSR + contactor.
- [ ] PV đứng yên gần setpoint trong khi heater hầu như không ON: không được false-trip Sensor Frozen.
- [ ] Ép High Temperature: SSR/contactactor OFF, vent + circulation ON.
- [ ] Ép Emergency Temperature: cắt ngay, còi/lockout đúng logic.
- [ ] Giả lập SSR welded ON: thermostat độc lập phải cắt contactor.
- [ ] Giả lập contactor welded: lớp thermal fuse/thermostat cấp cao hơn phải loại bỏ nguồn heater.

## 7. Turning / actuator

- [ ] Manual LEFT/RIGHT dừng đúng limit.
- [ ] Hai limit cùng active -> fault, motor không tiếp tục chạy.
- [ ] Không tới limit trong `turnMaxRunSec` -> fault và output OFF.
- [ ] Sau fault cơ khí cần kiểm tra, ACK thông thường không được tự bỏ interlock nếu policy yêu cầu Test Mode.
- [ ] Relay/contactors không chatter khi PID thay đổi; PID chỉ time-proportion SSR.

## 8. Power loss / recovery

- [ ] Mất nguồn ESP32 ngoài mẻ -> boot lại sạch, không tự tạo mẻ giả.
- [ ] Mất nguồn giữa mẻ -> RTC/EEPROM recovery đúng policy.
- [ ] Nếu cấu hình yêu cầu xác nhận tiếp tục, heater không tự bật trước xác nhận.
- [ ] ATtiny nhận đúng trạng thái batch trước khi thử mất điện.
- [ ] Mất nguồn chính giữa mẻ -> mạch còi độc lập hoạt động theo thiết kế.
- [ ] Có điện lại -> Cloud/HMI phản ánh đúng trạng thái resume.

## 9. Watchdog / supervisor

- [ ] Cố tình block control loop trong build test: Supervisor latch trip -> suspend control -> safe outputs -> restart.
- [ ] Cố tình block HMI đủ ngưỡng fatal: safe outputs trước restart.
- [ ] Sau watchdog/reset, không có xung ON heater ngoài ý muốn trong quá trình boot.
- [ ] Serial ghi được reset reason.

## 10. OTA

- [ ] Check firmware không tự flash khi chưa xác nhận HMI.
- [ ] Firmware sai SHA-256 -> abort, boot firmware cũ.
- [ ] Firmware sai signature -> abort, boot firmware cũ.
- [ ] Mất Wi-Fi giữa download -> abort, firmware hiện hành còn nguyên.
- [ ] Reboot sau OTA hợp lệ -> firmware mới boot đúng và Cloud/MQTT reconnect.
- [ ] Manual rollback -> quay lại slot firmware trước nếu target hợp lệ.

## 11. Soak test 72 giờ

Trong 72 giờ:

- Duy trì nhiệt quanh setpoint thực tế.
- Bật batch và turning với chu kỳ rút ngắn trong môi trường test để tăng số lần chuyển trạng thái.
- Mỗi 6-12 giờ tạo một sự kiện: Wi-Fi off/on, MQTT unavailable, Cloud unavailable hoặc browser reconnect.
- Ghi lại `min free heap`, `max control cycle`, `max HMI cycle`, reset reason, lỗi sensor, MQTT reconnect count và Cloud backoff.
- Không chấp nhận memory leak có xu hướng giảm heap liên tục.
- Không chấp nhận watchdog/reset không giải thích được.
- Không chấp nhận output heater thay đổi vì lỗi network/HMI.

### Tiêu chí PASS 72h

- [ ] 0 lần heater bị ON ngoài yêu cầu của MachineController/OutputArbiter.
- [ ] 0 reset không giải thích được.
- [ ] 0 deadlock/treo HMI kéo dài vượt policy supervisor.
- [ ] Tất cả network outage tự phục hồi.
- [ ] MQTT/Cloud lỗi không làm tăng control loop vượt deadline trip.
- [ ] Không xuất hiện suy giảm heap tuyến tính theo thời gian.

## 12. Hồ sơ mỗi máy trước xuất xưởng

Ghi tối thiểu:

- Device ID.
- Firmware version + Git commit/tag.
- Ngày test.
- Kết quả sensor/high/emergency/SSR-welded test.
- Kết quả 2 limit turning.
- Kết quả mất điện/recovery.
- Kết quả provisioning + reset PIN.
- Người kiểm tra.

Nếu một mục an toàn bắt buộc thất bại, **không xuất xưởng** dù web/MQTT vẫn hoạt động bình thường.
