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
        
    @Slot()
    def update_time(self):
        if not self._settings_manager.showClock:
            self.timeChanged.emit("")
            return
            
        current_time = datetime.now()
        show_seconds = self._settings_manager.clockShowSeconds
        if self._settings_manager.clockFormat24Hour:
            time_str = current_time.strftime("%H:%M:%S" if show_seconds else "%H:%M")
        else:
            hour_min = current_time.strftime("%I:%M:%S" if show_seconds else "%I:%M")
            am_pm = current_time.strftime("%p").upper()  # Force uppercase
            time_str = f"{hour_min} {am_pm}"

        self.timeChanged.emit(time_str)