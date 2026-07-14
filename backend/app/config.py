from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    app_name: str = "星羽汇聚 API"
    api_prefix: str = "/api/v1"
    secret_key: str = "change-me-in-production-xingyu-2026"
    access_token_expire_minutes: int = 60 * 24 * 7
    database_url: str = "sqlite+aiosqlite:///./data/xingyu.db"
    upload_dir: str = "./data/uploads"
    cors_origins: str = "*"

    class Config:
        env_file = ".env"


settings = Settings()
