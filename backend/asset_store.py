"""AssetStore — hands QML a file:// URL for a file under frontend/assets/.

Python peer of src/util/assetstore.{h,cpp}. The C++ version exists because
Quick3D's RuntimeLoader (Assimp) cannot read files inside an Android APK and
has to extract them first; the Python backend is desktop-only, so this is just
the path arithmetic. QML calls ``assetStore.localUrl("music_devices/cd.glb")``
on either backend.
"""
import os

from PySide6.QtCore import QObject, QUrl, Slot

from backend.logging_config import get_logger

logger = get_logger(__name__)


class AssetStore(QObject):
    def __init__(self, source_dir: str, parent=None):
        super().__init__(parent)
        self._source_dir = os.path.abspath(source_dir)

    @Slot(str, result=QUrl)
    def localUrl(self, relative_path: str) -> QUrl:
        if not relative_path or ".." in relative_path:
            return QUrl()
        path = os.path.normpath(os.path.join(self._source_dir, relative_path))
        if not os.path.exists(path):
            logger.warning("Asset not found: %s", path)
        return QUrl.fromLocalFile(path)
