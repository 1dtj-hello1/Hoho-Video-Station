const CHUNK_SIZE = 5 * 1024 * 1024;  // 5MB 一片
const MAX_CONCURRENT = 6;             // 同时上传6片

function log(msg) {
    const logDiv = document.getElementById('log');
    const time = new Date().toLocaleTimeString();
    logDiv.innerHTML += `[${time}] ${msg}\n`;
    logDiv.scrollTop = logDiv.scrollHeight;
    console.log(msg);
}

async function uploadFile(file) {
    const totalChunks = Math.ceil(file.size / CHUNK_SIZE);
    log(`开始上传: ${file.name} (${(file.size / 1024 / 1024).toFixed(2)} MB, ${totalChunks} 个分片)`);

    // 1. 初始化会话
    log('→ 初始化上传会话...');
    const initRes = await fetch('/upload/init', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
            filename: file.name,
            total_chunks: totalChunks
        })
    });
    const initData = await initRes.json();
    if (!initData.success) throw new Error(initData.message);
    const uploadId = initData.uploadId;
    log(`✓ 会话已创建: ${uploadId}`);

    // 2. 准备所有分片
    const chunks = [];
    for (let i = 0; i < totalChunks; i++) {
        const start = i * CHUNK_SIZE;
        const end = Math.min(start + CHUNK_SIZE, file.size);
        chunks.push({ index: i, data: file.slice(start, end) });
    }

    // 3. 并发上传
    let completed = 0;
    let failedChunks = new Set();

    const updateProgress = () => {
        const percent = (completed / totalChunks * 100).toFixed(1);
        document.getElementById('progress-bar').style.width = percent + '%';
        document.getElementById('status').textContent = `进度: ${percent}% (${completed}/${totalChunks})`;
    };

    const uploadOneChunk = async (chunk) => {
        const { index, data } = chunk;
        const startTime = Date.now();
        const url = `/upload/chunk?uploadId=${uploadId}&chunkIndex=${index}&totalChunks=${totalChunks}`;

        try {
            const response = await fetch(url, {
                method: 'POST',
                headers: { 'Content-Type': 'application/octet-stream' },
                body: data
            });

            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            const result = await response.json();
            if (!result.success) throw new Error(result.message);

            completed++;
            updateProgress();
            log(`  ✓ 分片 ${index}/${totalChunks - 1} 完成 (${(data.size / 1024).toFixed(1)}KB, ${Date.now() - startTime}ms)`);
        } catch (error) {
            log(`  ✗ 分片 ${index} 失败: ${error.message}`);
            throw error;
        }
    };

    const uploadWithRetry = async (chunk, maxRetries = 3) => {
        for (let attempt = 1; attempt <= maxRetries; attempt++) {
            try {
                await uploadOneChunk(chunk);
                return true;
            } catch (error) {
                if (attempt === maxRetries) {
                    log(`  ✗ 分片 ${chunk.index} 重试 ${maxRetries} 次后仍然失败`);
                    return false;
                }
                log(`  ↻ 分片 ${chunk.index} 重试 (${attempt}/${maxRetries})`);
                await new Promise(r => setTimeout(r, 1000 * attempt));
            }
        }
        return false;
    };

    // 并发控制队列
    const queue = [...chunks];
    const workers = [];
    for (let i = 0; i < Math.min(MAX_CONCURRENT, totalChunks); i++) {
        workers.push((async () => {
            while (queue.length > 0) {
                const chunk = queue.shift();
                const success = await uploadWithRetry(chunk);
                if (!success) failedChunks.add(chunk.index);
            }
        })());
    }

    await Promise.all(workers);
    if (failedChunks.size > 0) {
        throw new Error(`以下分片上传失败: ${[...failedChunks].join(', ')}`);
    }

    log(`✓ 所有 ${totalChunks} 个分片上传完成`);

    // 4. 合并文件
    log('→ 请求合并文件...');
    const completeRes = await fetch('/upload/complete', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ uploadId: uploadId })
    });
    const completeData = await completeRes.json();

    if (completeData.success) {
        log(`✅ 上传成功！`);
        log(`   文件: ${completeData.filename}`);
        log(`   大小: ${(completeData.size / 1024 / 1024).toFixed(2)} MB`);
        log(`   地址: ${completeData.url}`);
        document.getElementById('status').textContent = '✅ 上传成功！';
    } else {
        throw new Error(completeData.message);
    }
}

// UI 事件
const dropzone = document.getElementById('dropzone');
const fileInput = document.createElement('input');
fileInput.type = 'file';
fileInput.accept = 'video/*';

dropzone.onclick = () => fileInput.click();
fileInput.onchange = (e) => {
    if (e.target.files.length) {
        uploadFile(e.target.files[0]).catch(err => {
            log(`❌ 错误: ${err.message}`);
            document.getElementById('status').textContent = `❌ 错误: ${err.message}`;
        });
    }
};

dropzone.ondragover = (e) => {
    e.preventDefault();
    dropzone.classList.add('drag');
};
dropzone.ondragleave = () => dropzone.classList.remove('drag');
dropzone.ondrop = (e) => {
    e.preventDefault();
    dropzone.classList.remove('drag');
    if (e.dataTransfer.files.length) {
        uploadFile(e.dataTransfer.files[0]).catch(err => {
            log(`❌ 错误: ${err.message}`);
        });
    }
};

log('就绪，点击或拖拽视频文件开始上传（并发分片模式）');
log(`分片大小: ${CHUNK_SIZE / 1024 / 1024}MB, 最大并发: ${MAX_CONCURRENT}`);