# Chính sách an toàn và bảo mật

Đây là phần mềm điều khiển thiết bị nhiệt/cơ khí. Không xem một lần build thành
công là bằng chứng đủ để đưa máy vào vận hành thực tế.

## Báo cáo vấn đề

Không đăng công khai credential, firmware production, ảnh tem có PIN hoặc log
chứa dữ liệu nhận dạng thiết bị. Hãy báo riêng cho chủ repository và kèm phiên
bản, commit, điều kiện tái hiện cùng ảnh/log đã che thông tin nhạy cảm.

## Bí mật và credential

- Không commit `secrets.h`, `.dev.vars`, private key, CA private key hoặc file
  `.bin` production.
- Mỗi máy dùng device secret và PIN xuất xưởng riêng; không tái sử dụng giữa
  các máy.
- Broker MQTT production phải dùng TLS và ACL theo đúng topic thiết bị.
- Credential đặt trong `config.js` có thể bị người dùng trình duyệt đọc; chỉ
  dùng token ngắn hạn hoặc quyền tối thiểu.
- Khi nghi ngờ credential cũ đã xuất hiện trong lịch sử Git/release, phải xoay
  credential ở broker/Worker; chỉ xóa khỏi commit mới là chưa đủ.

## Cổng mạng

- ArduinoOTA bị tắt theo mặc định và bị cấm trong build production.
- Firmware từ chối TLS nếu thiếu CA, trừ bản test chủ động bật
  `MAYAP_ALLOW_INSECURE_TLS=1`.
- Worker giới hạn origin browser, kích thước JSON và số lần thử PIN.
- Kênh OTA qua GitHub công khai mặc định tắt vì firmware hiện vẫn có thể chứa
  credential build-time.

## Điều kiện trước khi phát hành

1. CI xanh và không có cảnh báo secret scan.
2. Review độc lập các thay đổi `machine_control.h`, `config.h` và schema EEPROM.
3. Test bàn thật các interlock quá nhiệt, mất cảm biến, xung đột hành trình,
   mất điện/khôi phục và watchdog.
4. Test broker ACL: web/thiết bị không đọc hoặc ghi được topic ngoài quyền.
5. Test Worker với PIN sai, khóa brute-force, Push hết hạn và origin sai.
6. Xác minh SHA-256 firmware qua kênh phân phối đã được phê duyệt.
