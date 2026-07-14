"""App OTA 版本检查。"""

import json
from pathlib import Path

from app.schemas import AppReleaseOut

router = APIRouter(prefix="/app", tags=["app"])

_RELEASE_FILE = Path(__file__).resolve().parent.parent / "releases" / "latest.json"


@router.get("/release", response_model=AppReleaseOut)
async def get_app_release():
    if not _RELEASE_FILE.is_file():
        raise HTTPException(status_code=404, detail="暂无版本信息")
    data = json.loads(_RELEASE_FILE.read_text(encoding="utf-8"))
    return {
        "versionCode": int(data.get("versionCode", 0)),
        "versionName": str(data.get("versionName", "")),
        "apkUrl": str(data.get("apkUrl", "")),
        "changelog": str(data.get("changelog", "")),
        "forceUpdate": bool(data.get("forceUpdate", False)),
    }
