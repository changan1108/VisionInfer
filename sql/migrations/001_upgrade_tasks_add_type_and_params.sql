USE vision_db;

ALTER TABLE tasks
    ADD COLUMN task_type VARCHAR(64) NOT NULL DEFAULT 'violation_detection' AFTER task_name,
    ADD COLUMN frame_interval INT NOT NULL DEFAULT 1 AFTER output_video_path,
    ADD COLUMN confidence_threshold DECIMAL(4,2) NOT NULL DEFAULT 0.50 AFTER frame_interval;

ALTER TABLE tasks
    ADD INDEX idx_tasks_type (task_type);
