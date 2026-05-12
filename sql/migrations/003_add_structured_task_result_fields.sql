USE vision_db;

ALTER TABLE tasks
    ADD COLUMN processed_frame_count INT NOT NULL DEFAULT 0 AFTER confidence_threshold,
    ADD COLUMN detection_count INT NOT NULL DEFAULT 0 AFTER processed_frame_count,
    ADD COLUMN real_inference_executed TINYINT(1) NOT NULL DEFAULT 0 AFTER detection_count,
    ADD COLUMN result_video_generated TINYINT(1) NOT NULL DEFAULT 0 AFTER real_inference_executed,
    ADD COLUMN used_model_name VARCHAR(128) DEFAULT '' AFTER result_video_generated,
    ADD COLUMN used_model_framework VARCHAR(32) DEFAULT '' AFTER used_model_name,
    ADD COLUMN video_build_mode VARCHAR(32) DEFAULT '' AFTER used_model_framework,
    ADD COLUMN inference_runtime_message TEXT AFTER video_build_mode;
