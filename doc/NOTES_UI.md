# Notes UI — Web 12.1.8

Nhánh `feature/notes-floating-ui` bổ sung nhật ký vận hành ở lớp giao diện Web. Nút ghi chú kéo bằng chuột/cảm ứng, snap cạnh trái/phải và giữ vị trí sau reload. Khung xem nhanh hiển thị bốn ghi chú mới nhất; “Xem tất cả” có tìm kiếm tiếng Việt và bộ lọc Mẻ / Máy, bảo trì.

Form dùng chung cho thêm/sửa, tiêu đề không bắt buộc tối đa 60 ký tự, nội dung bắt buộc tối đa 300 ký tự. Loại Mẻ mặc định chỉ khi máy được chọn có snapshot còn mới và xác nhận mẻ đang chạy. Mẻ đã dừng vẫn cho sửa ghi chú lịch sử, nhưng không tạo liên kết mẻ mới. Đổi máy khi đang nhập sẽ chặn lưu vào nhầm máy. Hủy/ESC/đóng với nội dung chưa lưu yêu cầu xác nhận; click ngoài không bỏ bản nháp. Xóa dùng dialog MAYAP hiện có. Nội dung người dùng được dựng bằng `textContent`.

## Giọng nói

Trong form, bấm **Nhập bằng giọng nói**, cho phép micro trong trình duyệt và nói tiếng Việt. Bấm **Dừng micro**, kiểm tra/sửa nội dung rồi lưu. Nhận dạng dùng `SpeechRecognition` hoặc `webkitSpeechRecognition`, ngôn ngữ `vi-VN`. Kết quả cuối cùng được chèn tại con trỏ hoặc thay đoạn đang chọn, giữ chữ đã nhập; kết quả tạm chỉ xuất hiện trong trạng thái nghe. Không tự mở hoặc tự khởi động lại micro.

Nút Lưu chờ phiên nhận dạng kết thúc. Đủ 300 ký tự sẽ dừng nhận dạng và báo giới hạn. Hủy, đóng, đổi máy, rời tab hoặc rời trang sẽ tắt micro và bỏ qua kết quả muộn. Lỗi quyền micro, thiết bị âm thanh, mạng và ngôn ngữ có thông báo trong form. Trình duyệt thiếu API hoặc trang không có secure context sẽ vô hiệu hóa nút micro; bàn phím vẫn hoạt động.

Nhận giọng nói phụ thuộc trình duyệt và dịch vụ của trình duyệt, có thể cần Internet và gửi âm thanh đến dịch vụ nhận dạng. Không thêm thư viện, CDN, khóa dịch vụ hoặc endpoint cho tính năng này. Tham khảo [SpeechRecognition — MDN](https://developer.mozilla.org/en-US/docs/Web/API/SpeechRecognition).

## Adapter lưu trữ

`notes.js` tách UI khỏi service; `MayapNotes.mount({ getContext, confirmAction, toast, service })` nhận một service có bốn phương thức async:

```js
listNotes(context)                   // Promise<Note[]>; newest first
createNote({ kind, title, body }, context)
updateNote(id, { kind, title, body }, context)
deleteNote(id, context)
```

`Note` hiện gồm `id`, `deviceId`, `kind` (`batch`/`machine`), `title`, `body`, `batchLabel`, `createdAt` và `updatedAt` (timestamp milliseconds; `null` khi chưa sửa). Sửa giữ `id` và thời điểm tạo. Danh sách và thao tác được phân theo `deviceId`.

**Hiện tại chỉ dùng `MemoryNotesStore` (Map trong RAM của trang). Reload/đóng trang làm mất ghi chú.** Không ghi nội dung vào localStorage, MQTT, ESP32, Preferences/NVS, EEPROM hay AT24C32. localStorage chỉ chứa `{ side, ratio }` tại `mayap.notes.position.v1` cho vị trí nút.

Điểm thay backend là tham số `service` trong lời gọi `mount` ở `app.js`. Khi tích hợp storage ESP32/cloud, thay adapter này và giữ UI. Backend thực cần tự xác thực quyền máy, kiểm tra input, cấp ID/thời gian và cung cấp ID mẻ thật; `batchLabel` hiện chỉ là nhãn giao diện, không phải khóa định danh mẻ. Giai đoạn này chưa định nghĩa hoặc thay giao thức lưu ghi chú trên ESP32.

`getContext` chỉ đọc thiết bị hiện tại, trạng thái online và `snapshot.runtime.batchRunning`. Hook `syncContext` trong `renderDevice` cập nhật ngữ cảnh; khi ngữ cảnh không đổi, hook trả về ngay. Vị trí được tính lại theo resize/visualViewport, cuộn trang và chuyển trang; kéo dùng Pointer Events + requestAnimationFrame, không thêm timer chạy liên tục.

## Các file thay đổi

| File | Nội dung |
| --- | --- |
| `index.html` | FAB SVG, panel, form, danh sách, dialog xem tất cả, nút micro, script Notes |
| `styles.css` | CSS scoped `.notes-*`, dùng token màu/theme hiện có, cuộn riêng và nút lưu luôn trong khung |
| `notes.js` | Controller UI, kéo/snap/focus, CRUD adapter RAM, tìm kiếm, nhập giọng nói |
| `app.js` | Mount Notes và hook đọc ngữ cảnh máy/mẻ |
| `sw.js` | Cache shell có `notes.js`; cache 12.1.8 |
| `release-manifest.json` | Đồng bộ Web 12.1.8 |
| `README.md` | Mô tả phiên bản và liên kết tài liệu |
| `tests/notes-store.test.cjs` | Phân máy, CRUD, ID/thời gian, validation và chèn giọng nói |
| `tools/test_web_experience.cjs` | Export fixture QA dùng lại; cho phép init script mô phỏng trình duyệt |
| `tools/test_notes_ui.cjs` | QA responsive, kéo/click, XSS, tìm kiếm, form/dialog, chuyển máy và trạng thái lỗi |
| `tools/test_notes_voice.cjs` | QA nhận giọng nói bằng sự kiện mô phỏng |
| `doc/NOTES_UI.md` | Kiến trúc, giới hạn thử nghiệm và cách kiểm tra |

## Kiểm tra

- `node --test tests/*.test.cjs`: 61/61 pass, gồm kiểm tra bảo toàn firmware, safety, protocol và giao dịch Web.
- `node --check` với `notes.js`, `app.js`, `sw.js` và hai công cụ QA Notes: pass.
- `python tools/check_release_sync.py` và `python tools/check_v381_reliability.py`: pass.
- Browser QA trên Chromium: 1920×1080, 1366×768, 768×1024, 390×844, 390×420, 844×390, 320×568; bổ sung light/dark ở desktop và điện thoại. Kéo chuột/cảm ứng, snap, phục hồi vị trí, không mở nhầm sau kéo, cuộn danh sách, focus trở lại FAB, nội dung 300 ký tự và không tràn ngang: pass.
- XSS với `<img ... onerror>`, `< > " ' &` được hiển thị nguyên văn, không tạo HTML chạy được. Thêm/sửa/xóa, giữ ID khi sửa, search bỏ dấu, lọc, loading/error/retry, dirty confirmation kể cả dialog lồng nhau, mẻ vừa dừng, chuyển máy đang nhập và RAM không lưu sau reload: pass.
- Voice QA mô phỏng: chỉ mở khi bấm, `vi-VN`, interim/final, không chèn trùng, dừng/lưu, từ chối quyền/lỗi mạng, giới hạn 300, hủy/kết quả muộn, đổi máy và fallback không hỗ trợ: pass. Chưa xác nhận độ chính xác nhận dạng bằng micro thật trên thiết bị người dùng; vùng bàn phím được kiểm tra bằng viewport thu nhỏ.

Để chạy Browser QA, phục vụ repo tại `http://127.0.0.1:8765`, cài/sử dụng Playwright có sẵn và chạy:

```sh
node tools/test_notes_ui.cjs work/notes-qa
node tools/test_notes_voice.cjs work/notes-qa
```

Có thể đặt `MAYAP_PLAYWRIGHT` tới package Playwright và `MAYAP_CHROME` tới binary Chromium/Chrome. Fixture mock toàn bộ transport máy và endpoint ngoài; không điều khiển máy thật. Voice QA không ghi âm hoặc kết nối dịch vụ nhận dạng thật.
