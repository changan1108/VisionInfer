USE vision_db;

CREATE TABLE IF NOT EXISTS videos (
    id INT PRIMARY KEY AUTO_INCREMENT,
    submitted_by VARCHAR(64) NOT NULL,
    original_filename VARCHAR(255) NOT NULL,
    stored_filename VARCHAR(255) NOT NULL,
    stored_path VARCHAR(255) NOT NULL,
    file_size_bytes BIGINT NOT NULL DEFAULT 0,
    duration DOUBLE NOT NULL DEFAULT 0,
    width INT NOT NULL DEFAULT 0,
    height INT NOT NULL DEFAULT 0,
    fps DOUBLE NOT NULL DEFAULT 0,
    uploaded_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uniq_videos_stored_filename (stored_filename),
    UNIQUE KEY uniq_videos_stored_path (stored_path),
    INDEX idx_videos_submitted_by (submitted_by),
    INDEX idx_videos_uploaded_at (uploaded_at)
);

ALTER TABLE tasks
    ADD COLUMN input_video_id INT NULL AFTER input_video_path,
    ADD INDEX idx_tasks_input_video_id (input_video_id),
    ADD CONSTRAINT fk_tasks_input_video_id FOREIGN KEY (input_video_id) REFERENCES videos(id);
