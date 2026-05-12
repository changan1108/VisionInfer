CREATE DATABASE IF NOT EXISTS vision_db
DEFAULT CHARACTER SET utf8mb4
DEFAULT COLLATE utf8mb4_unicode_ci;

USE vision_db;

CREATE TABLE IF NOT EXISTS users (
    id INT NOT NULL AUTO_INCREMENT,
    username VARCHAR(50) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci NOT NULL,
    password VARCHAR(100) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci NOT NULL,
    create_time TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
    last_login TIMESTAMP NULL DEFAULT NULL COMMENT '上次登录时间',
    nickname VARCHAR(50) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '姓名/昵称',
    employee_id CHAR(3) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci DEFAULT NULL COMMENT '工号(000-999)',
    email VARCHAR(100) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '电子邮箱',
    phone CHAR(11) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '联系电话',
    department VARCHAR(100) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '所属中心',
    location VARCHAR(100) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '地理位置',
    timezone VARCHAR(50) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '时区',
    bio VARCHAR(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '个人简介(20字左右)',
    role VARCHAR(50) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '职能角色',
    PRIMARY KEY (id),
    UNIQUE KEY username (username),
    UNIQUE KEY employee_id (employee_id)
);

CREATE TABLE IF NOT EXISTS models (
    id INT PRIMARY KEY AUTO_INCREMENT,
    model_name VARCHAR(128) NOT NULL,
    file_path VARCHAR(255) NOT NULL,
    framework VARCHAR(32) NOT NULL DEFAULT 'onnx',
    is_active TINYINT(1) NOT NULL DEFAULT 0,
    uploaded_by VARCHAR(64) DEFAULT '',
    uploaded_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY uniq_model_name (model_name)
);

CREATE TABLE IF NOT EXISTS tasks (
    id BIGINT PRIMARY KEY AUTO_INCREMENT,
    task_name VARCHAR(128) DEFAULT '',
    task_type VARCHAR(64) NOT NULL,
    submitted_by VARCHAR(64) NOT NULL,
    input_video_path VARCHAR(255) NOT NULL,
    output_video_path VARCHAR(255) DEFAULT '',
    video_duration DOUBLE NOT NULL DEFAULT 0,
    video_width INT NOT NULL DEFAULT 0,
    video_height INT NOT NULL DEFAULT 0,
    video_fps DOUBLE NOT NULL DEFAULT 0,
    frame_interval INT NOT NULL DEFAULT 1,
    confidence_threshold DECIMAL(4,2) NOT NULL DEFAULT 0.50,
    processed_frame_count INT NOT NULL DEFAULT 0,
    detection_count INT NOT NULL DEFAULT 0,
    real_inference_executed TINYINT(1) NOT NULL DEFAULT 0,
    result_video_generated TINYINT(1) NOT NULL DEFAULT 0,
    used_model_name VARCHAR(128) DEFAULT '',
    used_model_framework VARCHAR(32) DEFAULT '',
    video_build_mode VARCHAR(32) DEFAULT '',
    inference_runtime_message TEXT,
    status VARCHAR(32) NOT NULL DEFAULT 'PENDING',
    result_summary TEXT,
    error_message TEXT,
    model_id INT NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    started_at DATETIME NULL,
    finished_at DATETIME NULL,
    INDEX idx_tasks_status (status),
    INDEX idx_tasks_type (task_type),
    INDEX idx_tasks_user (submitted_by),
    INDEX idx_tasks_created_at (created_at),
    CONSTRAINT fk_tasks_model_id FOREIGN KEY (model_id) REFERENCES models(id)
);
