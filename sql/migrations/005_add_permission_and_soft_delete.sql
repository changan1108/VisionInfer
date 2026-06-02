USE vision_db;

ALTER TABLE users
    ADD COLUMN permission_level TINYINT NOT NULL DEFAULT 0 COMMENT '权限等级：0=普通操作员，1=管理员' AFTER role,
    ADD COLUMN account_status VARCHAR(32) NOT NULL DEFAULT 'active' COMMENT '账号状态：active/disabled' AFTER permission_level;

ALTER TABLE models
    ADD COLUMN is_deleted TINYINT(1) NOT NULL DEFAULT 0 AFTER updated_at,
    ADD COLUMN deleted_at DATETIME NULL AFTER is_deleted,
    ADD COLUMN deleted_by VARCHAR(64) DEFAULT '' AFTER deleted_at,
    ADD INDEX idx_models_is_deleted (is_deleted);

ALTER TABLE videos
    ADD COLUMN is_deleted TINYINT(1) NOT NULL DEFAULT 0 AFTER uploaded_at,
    ADD COLUMN deleted_at DATETIME NULL AFTER is_deleted,
    ADD COLUMN deleted_by VARCHAR(64) DEFAULT '' AFTER deleted_at,
    ADD INDEX idx_videos_is_deleted (is_deleted);

ALTER TABLE tasks
    ADD COLUMN is_deleted TINYINT(1) NOT NULL DEFAULT 0 AFTER finished_at,
    ADD COLUMN deleted_at DATETIME NULL AFTER is_deleted,
    ADD COLUMN deleted_by VARCHAR(64) DEFAULT '' AFTER deleted_at,
    ADD COLUMN cancel_requested TINYINT(1) NOT NULL DEFAULT 0 AFTER deleted_by,
    ADD COLUMN cancelled_at DATETIME NULL AFTER cancel_requested,
    ADD INDEX idx_tasks_is_deleted (is_deleted),
    ADD INDEX idx_tasks_cancel_requested (cancel_requested);
