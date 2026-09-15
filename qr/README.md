# Tem Device ID và PIN

Tạo tem mới bằng:

```bash
python3 generate_label.py MAP-XXXXXXXXXXXX PIN_6_SO
```

PIN phải là mã xuất xưởng riêng đúng 6 chữ số và phải trùng
`MAYAP_FACTORY_PIN` của đúng máy. Không commit ảnh tem production vì ảnh chứa
thông tin dùng để liên kết thiết bị.

Hai ảnh `MAP-441BF6E051D0-*` đang có trong repository là mẫu lịch sử chỉ chứa
Device ID, không phải tem v3.8.0 sẵn sàng đem in. Hãy tạo lại sau khi chốt PIN
thật của máy.
