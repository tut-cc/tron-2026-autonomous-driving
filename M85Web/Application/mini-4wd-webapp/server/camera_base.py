"""
カメラ映像プロバイダの基底クラス
実機およびモックの共通インターフェース
"""
from abc import ABC, abstractmethod

class BaseCameraProvider(ABC):
    @abstractmethod
    def get_frame(self) -> bytes:
        """
        最新の画像フレームを返す。
        MJPEG ストリーミング (/video_feed) で利用される。
        フレームがない場合は b"" または None を返す。
        """
        pass
