USE vision_db;

ALTER TABLE tasks
    ADD COLUMN video_duration DOUBLE NOT NULL DEFAULT 0 AFTER output_video_path,
    ADD COLUMN video_width INT NOT NULL DEFAULT 0 AFTER video_duration,
    ADD COLUMN video_height INT NOT NULL DEFAULT 0 AFTER video_width,
    ADD COLUMN video_fps DOUBLE NOT NULL DEFAULT 0 AFTER video_height;
