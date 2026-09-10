from PySide6.QtCore import QObject, Signal, Slot, QTimer
from datetime import datetime

class Clock(QObject):
    timeChanged = Signal(str)
    
    def __init__(self, settings_manager):
        super().__init__()
        self._settings_manager = settings_manager
        self.timer = QTimer()
        self.timer.timeout.connect(self.update_time)
        self.timer.start(1000)  # Update every second
        self._last_text = None   # last emitted text; emit only on change
        
    @Slot()
    def update_time(self):
        if not self._settings_manager.showClock:
            self._emit_if_changed("")
            return
            
        current_time = datetime.now()
        show_seconds = self._settings_manager.clockShowSeconds
        if self._settings_manager.clockFormat24Hour:
            time_str = current_time.strftime("%H:%M:%S" if show_seconds else "%H:%M")
        else:
            hour_min = current_time.strftime("%I:%M:%S" if show_seconds else "%I:%M")
            am_pm = current_time.strftime("%p").upper()  # Force uppercase
            time_str = f"{hour_min} {am_pm}"

        self._emit_if_changed(time_str)

    def _emit_if_changed(self, text):
        # Without seconds the text changes once a minute; re-evaluating every
        # binding that shows the clock 60 times for the same string is waste.
        if text == self._last_text:
            return
        self._last_text = text
        self.timeChanged.emit(text)