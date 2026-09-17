# Yêu cầu an toàn nhiệt bắt buộc cho MAYAP v3.8.0

## Phạm vi

Firmware chỉ là một lớp bảo vệ. Không được dùng ESP32, cảm biến chính hoặc SSR
làm lớp cắt nhiệt duy nhất.

## Chuỗi công suất bắt buộc

Nguồn heater phải đi tuần tự qua:

1. Cầu chì nhiệt dùng một lần.
2. Thermostat/rơ-le nhiệt cơ độc lập với ESP32.
3. Contactor an toàn.
4. SSR điều khiển công suất.
5. Điện trở gia nhiệt.

Thermostat phải có cảm biến riêng và tiếp điểm phải cắt trực tiếp cuộn contactor.
Không đưa thermostat qua phần mềm rồi mới quyết định cắt.

## Thay đổi phần mềm v3.8.0

- Mất, sai hoặc nghi ngờ cảm biến: SSR OFF và contactor nhiệt OFF.
- Nhiệt độ cao: SSR OFF, contactor OFF, quạt thông gió và tuần hoàn ON.
- Nhiệt khẩn cấp: cắt tức thời, còi và khóa vận hành theo logic hiện có.
- Công tắc nhiệt vật lý chỉ là điều kiện cho phép bật; không thể vượt qua fault.
- Sensor Frozen vẫn là cảnh báo chẩn đoán cho đến khi có cảm biến thứ hai để
  phân biệt ổn định thật với cảm biến đứng giá.

## Kiểm thử nghiệm thu máy thật

- Chập SSR ở trạng thái dẫn: thermostat độc lập phải cắt contactor.
- Rút cảm biến: contactor phải nhả.
- Chập cảm biến: contactor phải nhả.
- Ép nhiệt vượt ngưỡng High: contactor phải nhả và hai quạt chạy.
- Treo/reset ESP32: đầu ra phải về trạng thái an toàn.
- Dính contactor: cầu chì nhiệt/thermostat cấp cao hơn phải ngắt nguồn heater.

Không được xuất xưởng nếu bất kỳ phép thử nào ở trên thất bại.
