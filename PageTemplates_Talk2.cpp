#include "PageTemplates.h"
#include "MainWindow.h"
#include "SettingsPageHtml.h"
#include <QSettings>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QCoreApplication>
#include <QUrl>
#include <QList>
#include <QPair>
#include <QByteArray>
#include <QLocale>

#include "PageTemplates_Internal.h"

// ==========================================================================
// PageTemplates_Talk2.cpp — storm://talk, часть 2 из 2
// Вторая половина текста шаблона Storm Talk (продолжение части 1 из
// PageTemplates_Talk.cpp). См. шапку того файла — там же описан новый
// протокол сигнализации для групповых комнат и допущение про сервер.
// ==========================================================================

QString buildTalkHtmlPart2() {
    return u8R"HTML(

            camBtn.addEventListener('click', () => {
                if (!localStream) return;
                camEnabled = !camEnabled;
                localStream.getVideoTracks().forEach(t => t.enabled = camEnabled);
                // БАГФИКС: тот же конфликт классов .off/.active, что и у mic-btn (см. часть 1) —
                // "active" никогда не снимался, поэтому кнопка камеры оставалась зелёной
                // даже при выключенной камере.
                camBtn.classList.toggle('off', !camEnabled);
                camBtn.classList.toggle('active', camEnabled);
                camBtn.innerText = camEnabled ? "📷" : "🚫";
                console.log("[TALK] Камера " + (camEnabled ? "включена" : "выключена"));
                broadcastToAllPeers({ type: 'state', mic: micEnabled, cam: camEnabled });
                renderParticipantsList();
            });

            // ===================== ДЕМОНСТРАЦИЯ ЭКРАНА =====================
            // Раньше здесь был выбор "весь экран" / "вкладка браузера" через
            // constraint displaySurface — на практике в этом браузере (QtWebEngine)
            // системный выбор источника всё равно показывает список экранов и окон,
            // а не отдельных вкладок (см. пикер в MainWindow_Tabs.cpp), так что
            // разделение только запутывало и не давало реальной разницы. Теперь
            // один тумблер: клик — выбираете источник в системном диалоге (экран
            // целиком или конкретное окно), повторный клик — выключить показ.
            //
            // Второе изменение: пока демонстрация активна, отдельное окошко
            // TalkPipOverlay (нативное, всегда поверх всех окон — см. TalkBridge.h)
            // показывает видео собеседника и то, что реально уходит от вас —
            // чтобы не терять контакт и не показать случайно лишнее, даже если вы
            // переключились в другое приложение и вкладка браузера не видна.

            let pipInterval = null;
            const pipPeerCanvas = document.createElement('canvas');
            pipPeerCanvas.width = 240; pipPeerCanvas.height = 135;
            const pipPeerCtx = pipPeerCanvas.getContext('2d');
            const pipSelfCanvas = document.createElement('canvas');
            pipSelfCanvas.width = 240; pipSelfCanvas.height = 135;
            const pipSelfCtx = pipSelfCanvas.getContext('2d');

            function startPipFeed() {
                if (pipInterval) return;
                // ~4 кадра в секунду и низкое разрешение — этого достаточно, чтобы
                // ориентироваться "кто где" и заметить случайно всплывшее окно,
                // но не грузит CPU так, как полноценное второе видео.
                pipInterval = setInterval(() => {
                    if (!window.talkBridge) return;
                    const firstPeer = peers.size ? peers.values().next().value : null;
                    if (firstPeer && firstPeer.videoEl.videoWidth) {
                        pipPeerCtx.drawImage(firstPeer.videoEl, 0, 0, 240, 135);
                        try { window.talkBridge.pushPeerFrame(pipPeerCanvas.toDataURL('image/jpeg', 0.5)); } catch (e) { /* мост мог отвалиться — не критично */ }
                    }
                    if (localVideo.videoWidth) {
                        pipSelfCtx.drawImage(localVideo, 0, 0, 240, 135);
                        try { window.talkBridge.pushSelfFrame(pipSelfCanvas.toDataURL('image/jpeg', 0.5)); } catch (e) { /* мост мог отвалиться — не критично */ }
                    }
                }, 250);
            }
            function stopPipFeed() {
                if (pipInterval) { clearInterval(pipInterval); pipInterval = null; }
            }

            screenBtn.addEventListener('click', () => {
                if (isScreenSharing) { stopScreenShare(); return; }
                if (peers.size === 0) {
                    showAlert("Сначала дождитесь участника в комнате — потом можно включить демонстрацию экрана.");
                    return;
                }
                startScreenShare();
            });

            async function startScreenShare() {
                try {
                    // audio:true — если источник поддерживает захват звука (звук окна/системы),
                    // браузер сам предложит переключатель в системном диалоге.
                    screenStream = await navigator.mediaDevices.getDisplayMedia({ video: true, audio: false });
                } catch (e) {
                    console.warn("[TALK] Демонстрация экрана отменена или недоступна: " + e);
                    return;
                }
                const screenTrack = screenStream.getVideoTracks()[0];
                const screenAudioTrack = screenStream.getAudioTracks()[0];
                isScreenSharing = true;
                screenBtn.classList.add('active');

                // Подменяем исходящий видео-трек (и, если есть, аудио-трек источника) у ВСЕХ
                // текущих участников комнаты сразу — раньше был один currentCall, теперь их
                // может быть несколько (групповой звонок).
                peers.forEach(entry => {
                    const videoSender = entry.pc.getSenders().find(s => s.track && s.track.kind === 'video');
                    if (videoSender) videoSender.replaceTrack(screenTrack);
                    if (screenAudioTrack) {
                        const audioSender = entry.pc.getSenders().find(s => s.track && s.track.kind === 'audio');
                        if (audioSender) audioSender.replaceTrack(screenAudioTrack);
                    }
                });
                localVideo.srcObject = screenStream;
                console.log("[TALK] Демонстрация экрана включена.");

                startPipFeed();
                if (window.talkBridge && window.talkBridge.screenShareStarted) {
                    try { window.talkBridge.screenShareStarted(); } catch (e) { console.warn("[TALK] talkBridge.screenShareStarted не отработал: " + e); }
                }

                // Если человек нажмёт системную кнопку "Остановить показ" — вернуть камеру
                screenTrack.onended = () => stopScreenShare();
            }

            function stopScreenShare() {
                if (screenStream) {
                    screenStream.getTracks().forEach(t => t.stop());
                    screenStream = null;
                }
                isScreenSharing = false;
                screenBtn.classList.remove('active');
                stopPipFeed();
                if (window.talkBridge && window.talkBridge.screenShareStopped) {
                    try { window.talkBridge.screenShareStopped(); } catch (e) { console.warn("[TALK] talkBridge.screenShareStopped не отработал: " + e); }
                }

                const canvasTrack = outgoingStream ? outgoingStream.getVideoTracks()[0] : null;
                const micTrack = localStream ? localStream.getAudioTracks()[0] : null;
                peers.forEach(entry => {
                    const videoSender = entry.pc.getSenders().find(s => s.track && s.track.kind === 'video');
                    if (videoSender && canvasTrack) videoSender.replaceTrack(canvasTrack);
                    const audioSender = entry.pc.getSenders().find(s => s.track && s.track.kind === 'audio');
                    if (audioSender && micTrack) audioSender.replaceTrack(micTrack);
                });
                localVideo.srcObject = outgoingStream;
                console.log("[TALK] Демонстрация экрана остановлена, возврат к камере/микрофону.");
            }

            // ===================== РЕАКЦИИ =====================

            reactionBtn.addEventListener('click', () => reactionBar.classList.toggle('open'));

            document.querySelectorAll('.reaction-bar button').forEach(btn => {
                btn.addEventListener('click', () => {
                    const emoji = btn.dataset.emoji;
                    showReaction(emoji, false);
                    broadcastToAllPeers({ type: 'reaction', emoji });
                });
            });

            function showReaction(emoji, fromRemote) {
                const el = document.createElement('div');
                el.className = 'floating-reaction';
                el.innerText = emoji;
                el.style.left = (fromRemote ? (20 + Math.random() * 15) : (55 + Math.random() * 15)) + '%';
                document.querySelector('.video-container').appendChild(el);
                setTimeout(() => el.remove(), 2000);
            }

            // ===================== ЧАТ =====================

            chatBtn.addEventListener('click', () => chatPanel.classList.toggle('open'));

            function addChatMessage(text, isMe) {
                const msgEl = document.createElement('div');
                msgEl.className = 'chat-msg ' + (isMe ? 'me' : 'them');
                msgEl.innerText = text;
                chatMessages.appendChild(msgEl);
                chatMessages.scrollTop = chatMessages.scrollHeight;
            }

            function sendChat() {
                const text = chatInput.value.trim();
                if (!text) return;
                addChatMessage(text, true);
                broadcastToAllPeers({ type: 'chat', text });
                chatInput.value = '';
            }
            document.getElementById('chat-send-btn').addEventListener('click', sendChat);
            chatInput.addEventListener('keydown', (e) => { if (e.key === 'Enter') sendChat(); });

            // Рассылает сообщение по data-каналам ВСЕХ участников комнаты — раньше был
            // один dataChannel на весь звонок, теперь у каждого peer'а свой.
            function sendChannelMessage(channel, payload) {
                if (channel && channel.readyState === 'open') {
                    try { channel.send(JSON.stringify(payload)); }
                    catch (e) { console.warn("[TALK] Не удалось отправить по каналу данных: " + e); }
                }
            }
            function broadcastToAllPeers(payload) {
                peers.forEach(entry => sendChannelMessage(entry.dataChannel, payload));
            }

            function wireDataChannel(peerId, channel) {
                const entry = peers.get(peerId);
                if (entry) entry.dataChannel = channel;
                channel.onmessage = (e) => {
                    try {
                        const data = JSON.parse(e.data);
                        if (data.type === 'reaction') {
                            showReaction(data.emoji, true);
                        } else if (data.type === 'chat') {
                            addChatMessage(data.text, false);
                        } else if (data.type === 'name') {
                            if (entry) {
                                entry.displayName = (data.name || '').slice(0, 24);
                                if (entry.labelEl) entry.labelEl.innerText = entry.displayName || ('Участник ' + peerId.slice(0, 4));
                                renderParticipantsList();
                            }
                        } else if (data.type === 'state') {
                            if (entry) {
                                entry.micEnabled = !!data.mic;
                                entry.camEnabled = !!data.cam;
                                renderParticipantsList();
                            }
                        } else if (data.type === 'hand') {
                            if (entry) {
                                entry.handRaised = !!data.raised;
                                if (entry.handBadgeEl) entry.handBadgeEl.classList.toggle('show', entry.handRaised);
                                renderParticipantsList();
                            }
                        } else if (data.type === 'draw') {
                            // Рисуем только если доска у нас сейчас открыта — если она закрыта,
                            // canvas нулевого размера, а история штрихов до открытия не хранится
                            // (простое решение осознанно без сервера синхронизации рисунка).
                            if (whiteboardOverlay.classList.contains('open')) wbDrawSegment(data.from, data.to, data.color, data.size);
                        } else if (data.type === 'draw-clear') {
                            if (wbCtx) wbCtx.clearRect(0, 0, whiteboardCanvas.width, whiteboardCanvas.height);
                        }
                    } catch (err) {
                        console.warn("[TALK] Не удалось разобрать сообщение канала данных: " + err);
                    }
                };
                channel.onopen = () => {
                    console.log("[TALK] Канал чата/реакций с " + peerId + " установлен.");
                    // Сразу представляемся новому участнику — иначе у него в списке будет
                    // "Участник xxxx" с неверными иконками микрофона/камеры до следующего
                    // изменения этих состояний у нас.
                    sendChannelMessage(channel, { type: 'name', name: myDisplayName });
                    sendChannelMessage(channel, { type: 'state', mic: micEnabled, cam: camEnabled });
                    if (handRaised) sendChannelMessage(channel, { type: 'hand', raised: true });
                };
            }

            // ===================== "ГОВОРИТ СЕЙЧАС" (индикатор активного микрофона) =====================
            // Используем Web Audio API: для каждого аудио-трека (свой и у каждого
            // собеседника) заводим AnalyserNode и раз в 200мс меряем RMS громкости.
            // Если она выше порога — подсвечиваем плитку янтарной рамкой. Разрешение/частота
            // сознательно грубые — это индикатор "кто сейчас говорит", а не аудиометр.
            const SPEAKING_THRESHOLD = 0.05;

            function getAudioMeterCtx() {
                if (!audioMeterCtx) {
                    audioMeterCtx = new (window.AudioContext || window.webkitAudioContext)();
                }
                if (audioMeterCtx.state === 'suspended') audioMeterCtx.resume().catch(() => {});
                return audioMeterCtx;
            }

            function createAnalyserFor(stream) {
                const audioTracks = stream.getAudioTracks ? stream.getAudioTracks() : [];
                if (!audioTracks.length) return null;
                try {
                    const ctx = getAudioMeterCtx();
                    const source = ctx.createMediaStreamSource(new MediaStream([audioTracks[0]]));
                    const node = ctx.createAnalyser();
                    node.fftSize = 256;
                    node.smoothingTimeConstant = 0.6;
                    source.connect(node);
                    return { node, data: new Uint8Array(node.frequencyBinCount) };
                } catch (e) {
                    console.warn("[TALK] Не удалось создать анализатор громкости: " + e);
                    return null;
                }
            }

            function getVolumeLevel(analyser) {
                if (!analyser) return 0;
                analyser.node.getByteTimeDomainData(analyser.data);
                let sumSquares = 0;
                for (let i = 0; i < analyser.data.length; i++) {
                    const v = (analyser.data[i] - 128) / 128;
                    sumSquares += v * v;
                }
                return Math.sqrt(sumSquares / analyser.data.length);
            }

            function startSpeakingDetectionLoop() {
                if (speakingLoopTimer) return;
                speakingLoopTimer = setInterval(() => {
                    if (localAnalyser) {
                        const level = getVolumeLevel(localAnalyser);
                        localVideoWrap.classList.toggle('speaking', micEnabled && level > SPEAKING_THRESHOLD);
                    }
                    peers.forEach(entry => {
                        if (!entry.analyser || !entry.tileEl) return;
                        const level = getVolumeLevel(entry.analyser);
                        entry.tileEl.classList.toggle('speaking', entry.micEnabled !== false && level > SPEAKING_THRESHOLD);
                    });
                }, 200);
            }

            // ===================== ПОЛНОЭКРАННЫЙ РЕЖИМ ПО ДВОЙНОМУ КЛИКУ =====================

            function toggleFullscreenTile(tile) {
                if (document.fullscreenElement === tile) {
                    document.exitFullscreen().catch(() => {});
                } else if (tile.requestFullscreen) {
                    tile.requestFullscreen().catch((e) => console.warn("[TALK] Не удалось развернуть на весь экран: " + e));
                }
            }

            // ===================== ПОДНЯТАЯ РУКА =====================
            // В отличие от летящих emoji-реакций (гаснут сами через 2 сек), значок руки
            // держится на плитке, пока участник не снимет его сам повторным нажатием.

            handBtn.addEventListener('click', () => {
                handRaised = !handRaised;
                handBtn.classList.toggle('active', handRaised);
                localHandBadge.classList.toggle('show', handRaised);
                broadcastToAllPeers({ type: 'hand', raised: handRaised });
                renderParticipantsList();
            });

            // ===================== СПИСОК УЧАСТНИКОВ =====================

            participantsBtn.addEventListener('click', () => {
                participantsPanel.classList.toggle('open');
                if (participantsPanel.classList.contains('open')) renderParticipantsList();
            });

            function buildParticipantRow(name, isHandRaised, micOn, camOn, isMe) {
                const row = document.createElement('div');
                row.className = 'participant-row';

                const top = document.createElement('div');
                top.className = 'participant-row-top';

                const nameSpan = document.createElement('span');
                nameSpan.className = 'participant-name';
                nameSpan.textContent = (isHandRaised ? '✋ ' : '') + name + (isMe ? ' (вы)' : '');

                const iconsSpan = document.createElement('span');
                iconsSpan.className = 'participant-icons';
                iconsSpan.textContent = (micOn ? '🎤' : '🔇') + ' ' + (camOn ? '📷' : '🚫');

                top.appendChild(nameSpan);
                top.appendChild(iconsSpan);
                row.appendChild(top);
                return row;
            }

            function renderParticipantsList() {
                participantsList.innerHTML = '';

                participantsList.appendChild(
                    buildParticipantRow(myDisplayName || 'Вы', handRaised, micEnabled, camEnabled, true)
                );

                peers.forEach((entry, peerId) => {
                    const name = entry.displayName || ('Участник ' + peerId.slice(0, 4));
                    const row = buildParticipantRow(name, !!entry.handRaised, entry.micEnabled !== false, entry.camEnabled !== false, false);

                    // Индивидуальная громкость этого конкретного участника — независимо
                    // от общего ползунка в тулбаре (тот теперь задаёт громкость только
                    // для НОВЫХ участников по умолчанию, см. createPeerConnectionFor).
                    const volumeInput = document.createElement('input');
                    volumeInput.type = 'range';
                    volumeInput.min = '0';
                    volumeInput.max = '100';
                    volumeInput.value = String(Math.round((entry.volume != null ? entry.volume : currentVolume) * 100));
                    volumeInput.title = 'Громкость этого участника';
                    volumeInput.addEventListener('input', () => {
                        entry.volume = parseInt(volumeInput.value, 10) / 100;
                        entry.videoEl.volume = entry.volume;
                    });
                    row.appendChild(volumeInput);

                    participantsList.appendChild(row);
                });
            }

            // ===================== ОБРАБОТКА ЗВУКА (шумоподавление и т.п.) =====================
            // getUserMedia({audio:true}) уже включает эти три вещи по умолчанию — тут даём
            // возможность их выключить (например, музыкантам/подкастерам, которым
            // агрессивное шумоподавление портит звук). Раз constraints нельзя надёжно
            // поменять на лету у всех браузеров через applyConstraints, переполучаем
            // аудио-трек заново и подменяем его везде через replaceTrack — тем же приёмом,
            // что и подмена видео-трека при демонстрации экрана.

            audioSettingsBtn.addEventListener('click', () => audioSettingsPanel.classList.toggle('open'));

            let applyingAudioSettings = false;
            async function applyAudioProcessingSettings() {
                if (!localStream || applyingAudioSettings) return;
                applyingAudioSettings = true;
                try {
                    const newAudioStream = await navigator.mediaDevices.getUserMedia({
                        audio: {
                            noiseSuppression: noiseSuppressionEnabled,
                            echoCancellation: echoCancellationEnabled,
                            autoGainControl: autoGainControlEnabled
                        }
                    });
                    const newTrack = newAudioStream.getAudioTracks()[0];
                    if (!newTrack) throw new Error("Новый аудио-трек не получен");
                    newTrack.enabled = micEnabled; // сохраняем текущее состояние "микрофон выкл/вкл"

                    const oldTrack = localStream.getAudioTracks()[0];
                    if (oldTrack) { localStream.removeTrack(oldTrack); oldTrack.stop(); }
                    localStream.addTrack(newTrack);

                    if (outgoingStream) {
                        const oldOutTrack = outgoingStream.getAudioTracks()[0];
                        if (oldOutTrack) outgoingStream.removeTrack(oldOutTrack);
                        outgoingStream.addTrack(newTrack);
                    }

                    // Старый анализатор ссылался на уже остановленный трек — пересоздаём.
                    localAnalyser = createAnalyserFor(new MediaStream([newTrack]));

                    // Если сейчас НЕ идёт демонстрация экрана — сразу подменяем трек и в
                    // звонке у всех участников. Во время демонстрации там уже стоит трек
                    // из screenStream — не трогаем, новый микрофонный трек и так встанет
                    // на своё место при следующей остановке показа (см. stopScreenShare).
                    if (!isScreenSharing) {
                        peers.forEach(entry => {
                            const audioSender = entry.pc.getSenders().find(s => s.track && s.track.kind === 'audio');
                            if (audioSender) audioSender.replaceTrack(newTrack);
                        });
                    }
                    console.log("[TALK] Обработка микрофона обновлена: шумоподавление=" + noiseSuppressionEnabled + " эхоподавление=" + echoCancellationEnabled + " автоусиление=" + autoGainControlEnabled);
                } catch (e) {
                    console.error("[TALK] Не удалось применить настройки обработки звука: " + e.message);
                    showAlert("Не удалось изменить настройки микрофона: " + e.message);
                } finally {
                    applyingAudioSettings = false;
                }
            }

            toggleNoiseSuppression.addEventListener('click', () => {
                noiseSuppressionEnabled = !noiseSuppressionEnabled;
                toggleNoiseSuppression.classList.toggle('on', noiseSuppressionEnabled);
                applyAudioProcessingSettings();
            });
            toggleEchoCancellation.addEventListener('click', () => {
                echoCancellationEnabled = !echoCancellationEnabled;
                toggleEchoCancellation.classList.toggle('on', echoCancellationEnabled);
                applyAudioProcessingSettings();
            });
            toggleAutoGain.addEventListener('click', () => {
                autoGainControlEnabled = !autoGainControlEnabled;
                toggleAutoGain.classList.toggle('on', autoGainControlEnabled);
                applyAudioProcessingSettings();
            });

            // ===================== ОБЩАЯ ДОСКА =====================
            // Рисование транслируется всем участникам через тот же data-канал, что чат и
            // реакции — отрезками, нормированными в 0..1 (у каждого свой размер canvas,
            // нормированные координаты рисуются правильно независимо от разрешения окна).
            // Ограничение: история рисунка не хранится на сервере — участник, зашедший
            // после начала рисования (или закрывший доску), не увидит то, что было до него.

            whiteboardBtn.addEventListener('click', () => {
  const opening = !whiteboardOverlay.classList.contains('open');
  whiteboardOverlay.classList.toggle('open');
  
  if (opening) {
    requestAnimationFrame(() => {
      setupWhiteboardCanvas();
    });
  }
            });
            wbCloseBtn.addEventListener('click', () => whiteboardOverlay.classList.remove('open'));

            document.querySelectorAll('.wb-color').forEach(btn => {
                btn.addEventListener('click', () => {
                    wbColor = btn.dataset.color;
                    document.querySelectorAll('.wb-color').forEach(b => b.classList.remove('active'));
                    btn.classList.add('active');
                });
            });
            wbSizeSlider.addEventListener('input', () => { wbSize = parseInt(wbSizeSlider.value, 10); });

            function setupWhiteboardCanvas() {
                // Подгоняем внутреннее разрешение canvas под реальный размер на экране —
                // без этого рисунок будет смещён/растянут относительно курсора.
                const rect = whiteboardCanvas.getBoundingClientRect();
                whiteboardCanvas.width = rect.width;
                whiteboardCanvas.height = rect.height;
                wbCtx = whiteboardCanvas.getContext('2d');
                wbCtx.lineCap = 'round';
                wbCtx.lineJoin = 'round';
            }

            function wbCanvasPoint(e) {
                const rect = whiteboardCanvas.getBoundingClientRect();
                const clientX = e.touches ? e.touches[0].clientX : e.clientX;
                const clientY = e.touches ? e.touches[0].clientY : e.clientY;
                return { x: (clientX - rect.left) / rect.width, y: (clientY - rect.top) / rect.height };
            }

            function wbDrawSegment(from, to, color, size) {
                if (!wbCtx || !from || !to) return;
                wbCtx.strokeStyle = color;
                wbCtx.lineWidth = size;
                wbCtx.beginPath();
                wbCtx.moveTo(from.x * whiteboardCanvas.width, from.y * whiteboardCanvas.height);
                wbCtx.lineTo(to.x * whiteboardCanvas.width, to.y * whiteboardCanvas.height);
                wbCtx.stroke();
            }

            function wbStart(e) {
                e.preventDefault();
                wbDrawing = true;
                wbLastPoint = wbCanvasPoint(e);
            }
            function wbMove(e) {
                if (!wbDrawing) return;
                e.preventDefault();
                const point = wbCanvasPoint(e);
                wbDrawSegment(wbLastPoint, point, wbColor, wbSize);
                broadcastToAllPeers({ type: 'draw', from: wbLastPoint, to: point, color: wbColor, size: wbSize });
                wbLastPoint = point;
            }
            function wbEnd() { wbDrawing = false; wbLastPoint = null; }

            whiteboardCanvas.addEventListener('mousedown', wbStart);
            whiteboardCanvas.addEventListener('mousemove', wbMove);
            window.addEventListener('mouseup', wbEnd);
            whiteboardCanvas.addEventListener('touchstart', wbStart, { passive: false });
            whiteboardCanvas.addEventListener('touchmove', wbMove, { passive: false });
            whiteboardCanvas.addEventListener('touchend', wbEnd);

            wbClearBtn.addEventListener('click', () => {
                if (wbCtx) wbCtx.clearRect(0, 0, whiteboardCanvas.width, whiteboardCanvas.height);
                broadcastToAllPeers({ type: 'draw-clear' });
            });

            // ===================== СИГНАЛЬНЫЙ СЕРВЕР И КОМНАТЫ =====================
            // Собственный сервер (server.py, эндпоинт /ws/talk/{room}) + свой STUN/TURN.
            // Протокол сообщений (все поля, кроме type, необязательны в зависимости от типа):
            //   { type: 'hello',     from }                    — "я в комнате, вот мой peerId"
            //   { type: 'bye',       from }                    — "я ухожу из комнаты"
            //   { type: 'offer',     from, to, offer }          — адресный SDP-offer
            //   { type: 'answer',    from, to, answer }         — адресный SDP-answer
            //   { type: 'candidate', from, to, candidate }       — адресный ICE-кандидат
            // Сервер, как и раньше, ничего не обязан знать про это — просто рассылает
            // каждое сообщение всем остальным сокетам в комнате (broadcast-relay).

            // Адрес вашего сигнального сервера (server.py, эндпоинт /ws/talk/{room}).
            // Если IP/порт сервера изменится — поменяйте здесь.
            const SIGNALING_HOST = 'storm-browser.online:8000';

            const rtcConfig = {
  iceServers: [
    { urls: 'stun:stun.l.google.com:19302' },
    {
      urls: 'turn:2.59.161.162:8007?transport=udp',
      username: 'storm_user',
      credential: 'o4Apiel9bA0jrS'
    },
    {
      urls: 'turn:2.59.161.162:8007?transport=tcp',
      username: 'storm_user',
      credential: 'o4Apiel9bA0jrS'
    }
  ]
};

            function generateRoomId() {
                return Math.random().toString(36).substring(2, 9);
            }

            // Отдельный генератор для peerId — используется только внутри сигнализации
            // (никогда не показывается пользователю), поэтому важнее уникальность, чем
            // краткость. crypto.randomUUID() есть во всех современных Chromium-движках;
            // на всякий случай (старые сборки QtWebEngine) — запасной вариант без него.
            function generatePeerId() {
                if (window.crypto && typeof crypto.randomUUID === 'function') return crypto.randomUUID();
                return generateRoomId() + '-' + generateRoomId() + '-' + Date.now().toString(36);
            }

            // 1. Получаем доступ к камере и микрофону
            navigator.mediaDevices.getUserMedia({ video: true, audio: true })
                .then(stream => {
                    localStream = stream;
                    rawVideo.srcObject = stream;

                    // Диагностика: что реально пришло с камеры и запустился ли элемент
                    // <video id="raw-video"> — раньше по этому месту не было вообще
                    // никаких логов, поэтому и не было видно, где рвётся цепочка
                    // "поток с камеры -> raw-video -> canvas -> маленькое превью".
                    const vt = stream.getVideoTracks()[0];
                    console.log("[TALK] Видео-трек камеры: " + (vt ? (vt.label + ", enabled=" + vt.enabled + ", readyState=" + vt.readyState + ", muted=" + vt.muted) : "ОТСУТСТВУЕТ (только аудио!)"));

                    rawVideo.addEventListener('loadedmetadata', () => {
                        console.log("[TALK] raw-video loadedmetadata, размер: " + rawVideo.videoWidth + "x" + rawVideo.videoHeight);
                    });
                    rawVideo.addEventListener('playing', () => {
                        console.log("[TALK] raw-video playing, размер: " + rawVideo.videoWidth + "x" + rawVideo.videoHeight);
                    });
                    rawVideo.play().catch(e => console.error("[TALK] rawVideo.play() отклонён: " + e.name + " " + e.message));

                    // Собираем исходящий поток: обработанное видео (canvas) + сырое аудио.
                    // Именно outgoingStream уходит в звонок и в собственное превью —
                    // то, что видите вы, видят и остальные участники комнаты.
                    const canvasStream = processCanvas.captureStream(30);
                    outgoingStream = new MediaStream([
                        ...canvasStream.getVideoTracks(),
                        ...stream.getAudioTracks()
                    ]);
                    localVideo.srcObject = outgoingStream;
                    localVideo.play().catch(e => console.error("[TALK] localVideo.play() отклонён: " + e.name + " " + e.message));

                    renderLoop();
                    initSegmenter(); // асинхронно, не блокирует звонок

                    // Анализатор громкости своего микрофона для индикатора "говорит сейчас".
                    localAnalyser = createAnalyserFor(stream);
                    startSpeakingDetectionLoop();

                    myOwnRoomId = generateRoomId();
                    myPeerId = generatePeerId();
                    myIdDisplay.innerText = myOwnRoomId;
                    joinRoom(myOwnRoomId); // ждём участников в своей комнате
                })
                .catch(err => {
                    setStatus("Ошибка доступа к камере/микрофону: " + err.name, "#ff5f5f");
                    // Выводим точное имя ошибки (NotAllowedError / NotFoundError / NotReadableError / OverconstrainedError
                    // и т.д.) вместо общего "не удалось" — это и есть диагноз, а не догадка.
                    showAlert("Не удалось получить доступ к камере/микрофону.\nКод ошибки: " + err.name + "\n\nЧастые причины:\n• NotReadableError — камера занята другой программой (Zoom/Teams/OBS и т.п.)\n• NotAllowedError — доступ к камере запрещён в Параметры Windows → Конфиденциальность → Камера → «Разрешить приложениям рабочего стола доступ к камере»\n• NotFoundError — камера не найдена системой");
                    console.error("[TALK] Ошибка getUserMedia: name=" + err.name + " message=" + err.message);
                });

            // 2. Подключение к комнате на нашем сигнальном сервере и обмен hello/offer/answer/candidate.
            //    targetRoomId — код комнаты (либо свой myOwnRoomId, либо чужой, введённый в поле).
            function joinRoom(targetRoomId) {
                currentRoomId = targetRoomId;
                isInOwnRoom = (targetRoomId === myOwnRoomId);
                if (peers.size === 0) {
                    setStatus(isInOwnRoom ? "В сети, ожидание участников..." : "Подключение к комнате...", isInOwnRoom ? "#56d39b" : "#ffc857");
                }

                // Caddy теперь терминирует TLS на этом порту и включает автоматический
                // HTTP->HTTPS редирект — обычный ws:// на редирект не реагирует и просто
                // рвётся, поэтому здесь обязательно wss:// (было ws://).
                ws = new WebSocket(`wss://${SIGNALING_HOST}/ws/talk/${targetRoomId}`);

                // Если за 15 секунд сокет так и не открылся — до сервера не достучались
                // (сервер лежит, неверный адрес, сеть/файрвол блокирует порт) — раньше здесь
                // можно было зависнуть без какой-либо ошибки вообще.
                const connectTimeout = setTimeout(() => {
                    if (ws && ws.readyState !== WebSocket.OPEN) {
                        setStatus("Нет связи с сервером (таймаут 15 сек). Проверьте сеть/адрес сервера.", "#ff5f5f");
                        console.error("[TALK] Таймаут подключения к сигнальному серверу wss://" + SIGNALING_HOST + "/ws/talk/" + targetRoomId);
                        callBtn.disabled = false;
                        remoteIdInput.disabled = false;
                        ws.close();
                    }
                }, 15000);

                ws.onopen = () => {
                    clearTimeout(connectTimeout);
                    reconnectAttempt = 0;
                    callBtn.disabled = false;
                    remoteIdInput.disabled = false;
                    console.log("[TALK] Подключено к сигнальному серверу, комната: " + targetRoomId);
                    // Каждый участник при входе (и при каждом переподключении) объявляет о себе —
                    // этого достаточно для обнаружения всех остальных в комнате: каждый получивший
                    // 'hello' от незнакомого peerId отвечает своим же 'hello' (см. ниже), так что
                    // все пары участников узнают друг о друге независимо от порядка входа.
                    sendSignal('hello', null);
                };

                ws.onmessage = async (event) => {
                    let msg;
                    try { msg = JSON.parse(event.data); } catch (e) { return; }
                    if (!msg || !msg.type) return;

                    if (msg.type === 'hello') {
                        if (!msg.from || msg.from === myPeerId) return;
                        if (!peers.has(msg.from)) sendSignal('hello', null);
                        maybeConnectToPeer(msg.from);
                        return;
                    }
                    if (msg.type === 'bye') {
                        if (msg.from) disconnectPeer(msg.from);
                        return;
                    }
                    // Остальные типы сообщений адресные — сервер рассылает их всем в комнате,
                    // поэтому здесь отфильтровываем то, что предназначено не нам.
                    if (msg.to && msg.to !== myPeerId) return;
                    const entry = msg.from ? peers.get(msg.from) : null;

                    if (msg.type === 'offer') {
                        try {
                            const e = entry || createPeerConnectionFor(msg.from, false);
                            await e.pc.setRemoteDescription(new RTCSessionDescription(msg.offer));
                            const answer = await e.pc.createAnswer();
                            await e.pc.setLocalDescription(answer);
                            sendSignal('answer', msg.from, { answer });
                        } catch (err) {
                            console.error("[TALK] Ошибка обработки offer от " + msg.from + ": " + err.message);
                        }
                    } else if (msg.type === 'answer') {
                        if (entry) {
                            try { await entry.pc.setRemoteDescription(new RTCSessionDescription(msg.answer)); }
                            catch (err) { console.error("[TALK] Ошибка обработки answer от " + msg.from + ": " + err.message); }
                        }
                    } else if (msg.type === 'candidate' && msg.candidate) {
                        if (entry) {
                            try { await entry.pc.addIceCandidate(new RTCIceCandidate(msg.candidate)); }
                            catch (e) { console.warn("[TALK] Не удалось добавить ICE-кандидат: " + e); }
                        }
                    }
                };

                ws.onerror = (e) => {
                    clearTimeout(connectTimeout);
                    console.error("[TALK] Ошибка WebSocket сигнального сервера (см. вкладку Network для подробностей)");
                };

                ws.onclose = () => {
                    console.warn("[TALK] Соединение с сигнальным сервером закрыто.");
                    // Намеренные закрытия (leaveRoom, переход в другую комнату) обнуляют
                    // ws.onclose ПЕРЕД close() — значит, если мы всё же оказались здесь,
                    // это неожиданный обрыв, и стоит попробовать восстановить связь.
                    // Уже установленные P2P-соединения (mesh) при этом НЕ закрываются —
                    // сигнальный сокет нужен только для согласования новых участников.
                    scheduleReconnect(targetRoomId);
                };
            }

            function sendSignal(type, to, extra) {
                if (!ws || ws.readyState !== WebSocket.OPEN) return;
                ws.send(JSON.stringify(Object.assign({ type, from: myPeerId, to: to || undefined }, extra || {})));
            }

            function scheduleReconnect(roomId) {
                if (reconnectTimer) return; // переподключение уже запланировано
                reconnectAttempt++;
                if (reconnectAttempt > 5) {
                    setStatus("Не удаётся восстановить связь с сервером после нескольких попыток.", "#ff5f5f");
                    console.error("[TALK] Превышено число попыток переподключения к сигнальному серверу.");
                    return;
                }
                const delay = Math.min(1000 * reconnectAttempt, 8000);
                if (peers.size === 0) {
                    setStatus("Обрыв связи с сервером, переподключение через " + Math.round(delay / 1000) + "с...", "#ffc857");
                }
                console.warn("[TALK] Переподключение через " + delay + "мс (попытка " + reconnectAttempt + ")");
                reconnectTimer = setTimeout(() => {
                    reconnectTimer = null;
                    joinRoom(roomId);
                }, delay);
            }

            // 3. Устанавливаем соединение с конкретным участником комнаты (mesh: по одному
            //    RTCPeerConnection на каждого остального участника).
            function createPeerConnectionFor(peerId, isOfferer) {
                const pc = new RTCPeerConnection(rtcConfig);
                const tileRefs = createTileFor(peerId);
                tileRefs.video.volume = currentVolume;
                const entry = {
                    pc, dataChannel: null,
                    videoEl: tileRefs.video, labelEl: tileRefs.label, handBadgeEl: tileRefs.handBadge, tileEl: tileRefs.tile,
                    displayName: '', micEnabled: true, camEnabled: true, handRaised: false,
                    volume: currentVolume, analyser: null
                };
                peers.set(peerId, entry);
                renderParticipantsList();

                if (outgoingStream) {
                    outgoingStream.getTracks().forEach(track => pc.addTrack(track, outgoingStream));
                }

                pc.onicecandidate = (e) => {
                    if (e.candidate) sendSignal('candidate', peerId, { candidate: e.candidate });
                };

                pc.ontrack = (e) => {
                    entry.videoEl.srcObject = e.streams[0];
                    // Анализатор громкости для индикатора "говорит сейчас" — создаём один раз,
                    // как только в потоке появляется аудио-трек (ontrack может сработать
                    // несколько раз, по разу на трек).
                    if (!entry.analyser) entry.analyser = createAnalyserFor(e.streams[0]);
                    setStatus("Звонок активен (E2E шифрование)", "#56d39b");
                    statusIndicator.style.animation = "none";
                    callBtn.style.display = 'none';
                    endBtn.style.display = 'inline-block';
                    if (!callStartTime) startCallTimer();
                    updateParticipantCount();
                };

                pc.oniceconnectionstatechange = () => {
                    console.log("[TALK] ICE state (" + peerId + "): " + pc.iceConnectionState);
                    if (pc.iceConnectionState === 'failed' || pc.iceConnectionState === 'closed') {
                        disconnectPeer(peerId);
                    }
                };

                if (isOfferer) {
                    const channel = pc.createDataChannel('storm-talk-data');
                    wireDataChannel(peerId, channel);
                } else {
                    pc.ondatachannel = (e) => wireDataChannel(peerId, e.channel);
                }
                return entry;
            }

            // Узнав о новом участнике (через 'hello'), решаем, кто из нас двоих создаёт offer —
            // без этого при одновременном обнаружении друг друга оба участника одновременно
            // создали бы offer ("glare"), что ломает согласование. Простое и надёжное правило:
            // offer создаёт тот, у кого peerId меньше в лексикографическом сравнении строк —
            // это всегда даёт один и тот же результат на обеих сторонах.
            async function maybeConnectToPeer(theirPeerId) {
                if (theirPeerId === myPeerId || peers.has(theirPeerId)) return;
                try {
                    const iAmOfferer = myPeerId < theirPeerId;
                    const entry = createPeerConnectionFor(theirPeerId, iAmOfferer);
                    if (iAmOfferer) {
                        const offer = await entry.pc.createOffer();
                        await entry.pc.setLocalDescription(offer);
                        sendSignal('offer', theirPeerId, { offer });
                    }
                } catch (e) {
                    console.error("[TALK] Не удалось создать соединение с " + theirPeerId + ": " + e.message);
                }
            }

            function disconnectPeer(peerId) {
                const entry = peers.get(peerId);
                if (!entry) return;
                entry.pc.close();
                peers.delete(peerId);
                removeTileFor(peerId);
                updateParticipantCount();
                renderParticipantsList();
                if (peers.size === 0) {
                    stopCallTimer();
                    callBtn.style.display = 'inline-block';
                    endBtn.style.display = isInOwnRoom ? 'none' : 'inline-block';
                    statusIndicator.style.animation = "pulse-anim 1.5s infinite";
                    setStatus(isInOwnRoom ? "В сети, ожидание участников..." : "Собеседники вышли из комнаты", isInOwnRoom ? "#56d39b" : "#ffc857");
                }
            }

            // ===================== ПЛИТКИ УЧАСТНИКОВ (UI) =====================

            function createTileFor(peerId) {
                emptyRoomHint.style.display = 'none';
                const tile = document.createElement('div');
                tile.className = 'remote-tile';
                tile.id = 'tile-' + peerId;
                tile.title = 'Двойной клик — развернуть на весь экран';

                const video = document.createElement('video');
                video.autoplay = true;
                video.playsInline = true;

                const label = document.createElement('div');
                label.className = 'overlay-text';
                label.innerText = 'Участник ' + peerId.slice(0, 4);

                const handBadge = document.createElement('span');
                handBadge.className = 'hand-badge';
                handBadge.innerText = '✋';

                tile.appendChild(video);
                tile.appendChild(label);
                tile.appendChild(handBadge);
                tile.addEventListener('dblclick', () => toggleFullscreenTile(tile));
                remoteGrid.appendChild(tile);

                return { video, label, handBadge, tile };
            }

            function removeTileFor(peerId) {
                const tile = document.getElementById('tile-' + peerId);
                if (tile) tile.remove();
                if (peers.size === 0) emptyRoomHint.style.display = 'block';
            }

            function updateParticipantCount() {
                const n = peers.size;
                if (n > 0) {
                    participantDisplay.style.display = 'inline';
                    participantDisplay.innerText = "👥 " + (n + 1) + " в комнате";
                } else {
                    participantDisplay.style.display = 'none';
                }
            }

            // 4. Вход в чужую комнату по коду
            callBtn.addEventListener('click', () => {
                const targetRoom = remoteIdInput.value.trim();
                if (!targetRoom) return showAlert("Введите код комнаты!");
                if (!outgoingStream) return showAlert("Камера ещё не инициализирована.");
                if (targetRoom === currentRoomId) return; // уже в этой комнате

                // БАГФИКС: раньше кнопку "Позвонить" можно было нажать повторно (или с другим
                // ID) прямо во время установления предыдущего соединения — старое подключение
                // при этом не закрывалось, и оба звонка начинали конфликтовать. Теперь на время
                // подключения к комнате кнопка и поле ввода блокируются, разблокируются в
                // ws.onopen / по таймауту.
                callBtn.disabled = true;
                remoteIdInput.disabled = true;

                leaveAllPeers();
                if (ws) { ws.onclose = null; ws.close(); ws = null; }
                joinRoom(targetRoom);
            });

            // 5. Создать новую комнату (сменить свой код, не дожидаясь перезагрузки страницы)
            newRoomBtn.addEventListener('click', async () => {
                if (peers.size > 0) {
                    const proceed = await showConfirm(
                        "Вы сейчас в разговоре. Создать новую комнату и покинуть текущий?",
                        "Да, создать", "Отмена"
                    );
                    if (!proceed) return;
                }
                leaveAllPeers();
                if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null; }
                if (ws) { ws.onclose = null; ws.close(); ws = null; }
                myOwnRoomId = generateRoomId();
                myIdDisplay.innerText = myOwnRoomId;
                remoteIdInput.disabled = false;
                joinRoom(myOwnRoomId);
            });

            // 6. Покинуть текущую комнату (закрыть все соединения с участниками) и вернуться в свою
            endBtn.addEventListener('click', leaveRoom);

            function leaveRoom() {
                sendSignal('bye', null); // best-effort уведомление остальных участников комнаты
                leaveAllPeers();
                if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null; }
                if (ws) { ws.onclose = null; ws.close(); ws = null; }
                if (isScreenSharing) stopScreenShare();
                if (isRecording) stopRecording();
                remoteIdInput.disabled = false;
                // возвращаемся в СВОЮ комнату ожидания
                joinRoom(myOwnRoomId);
            }

            function leaveAllPeers() {
                peers.forEach(entry => entry.pc.close());
                peers.clear();
                remoteGrid.querySelectorAll('.remote-tile').forEach(t => t.remove());
                emptyRoomHint.style.display = 'block';
                stopCallTimer();
                updateParticipantCount();
                renderParticipantsList();
                callBtn.style.display = 'inline-block';
                endBtn.style.display = 'none';
                statusIndicator.style.animation = "pulse-anim 1.5s infinite";
            }

            // Лучшее из возможного уведомление остальных участников, если вкладку/приложение
            // просто закрыли, не нажимая "Покинуть комнату" — без этого их плитки исчезнут
            // только после таймаута ICE (десятки секунд), а не сразу.
            window.addEventListener('beforeunload', () => {
                if (ws && ws.readyState === WebSocket.OPEN) {
                    try { ws.send(JSON.stringify({ type: 'bye', from: myPeerId })); } catch (e) { /* стараемся, но не гарантируем */ }
                }
            });

            // ===================== ТАЙМЕР ЗВОНКА =====================
            function startCallTimer() {
                callStartTime = Date.now();
                durationDisplay.style.display = 'inline';
                if (durationTimer) clearInterval(durationTimer);
                durationTimer = setInterval(updateCallDuration, 1000);
                updateCallDuration();
            }
            function updateCallDuration() {
                if (!callStartTime) return;
                const secs = Math.floor((Date.now() - callStartTime) / 1000);
                const mm = String(Math.floor(secs / 60)).padStart(2, '0');
                const ss = String(secs % 60).padStart(2, '0');
                durationDisplay.innerText = "⏱ " + mm + ":" + ss;
            }
            function stopCallTimer() {
                if (durationTimer) { clearInterval(durationTimer); durationTimer = null; }
                callStartTime = null;
                durationDisplay.style.display = 'none';
                durationDisplay.innerText = '';
            }

            // ===================== ЛОКАЛЬНАЯ ЗАПИСЬ ЗВОНКА =====================
            // Упрощение: как и раньше, в кадр записи попадают ваше видео и видео ОДНОГО
            // собеседника (первого в списке участников) — полная сетка на N участников
            // при записи не собирается. Если нужна запись всей сетки — скажите, добавим.
            recordBtn.addEventListener('click', () => {
                if (!isRecording) startRecording(); else stopRecording();
            });

            function startRecording() {
                if (!outgoingStream) { showAlert("Камера ещё не готова."); return; }
                try {
                    recordCanvas = document.createElement('canvas');
                    recordCanvas.width = 1280;
                    recordCanvas.height = 720;
                    recordCtx = recordCanvas.getContext('2d');

                    recordAudioCtx = new (window.AudioContext || window.webkitAudioContext)();
                    const dest = recordAudioCtx.createMediaStreamDestination();
                    if (localStream && localStream.getAudioTracks().length) {
                        recordAudioCtx.createMediaStreamSource(new MediaStream(localStream.getAudioTracks())).connect(dest);
                    }
                    const firstPeer = peers.size ? peers.values().next().value : null;
                    if (firstPeer && firstPeer.videoEl.srcObject && firstPeer.videoEl.srcObject.getAudioTracks && firstPeer.videoEl.srcObject.getAudioTracks().length) {
                        recordAudioCtx.createMediaStreamSource(new MediaStream(firstPeer.videoEl.srcObject.getAudioTracks())).connect(dest);
                    }

                    const drawRecordFrame = () => {
                        const firstPeer = peers.size ? peers.values().next().value : null;
                        const remoteVideoEl = firstPeer ? firstPeer.videoEl : null;

                        if (isScreenSharing && localVideo.videoWidth) {
                            // Во время демонстрации в запись идёт ПОЛНЫЙ показываемый кадр —
                            // то же самое, что видит собеседник — а не привычная раскладка
                            // веб-камер 50/50 (иначе половина записи была бы просто пустой).
                            // Видео собеседника накладывается мелко в углу как "камера реакции".
                            recordCtx.fillStyle = '#000';
                            recordCtx.fillRect(0, 0, recordCanvas.width, recordCanvas.height);
                            recordCtx.drawImage(localVideo, 0, 0, recordCanvas.width, recordCanvas.height);
                            if (remoteVideoEl && remoteVideoEl.videoWidth) {
                                const cw = recordCanvas.width * 0.22, ch = cw * 9 / 16;
                                const cx = recordCanvas.width - cw - 16, cy = recordCanvas.height - ch - 16;
                                recordCtx.fillStyle = 'rgba(0,0,0,0.6)';
                                recordCtx.fillRect(cx - 3, cy - 3, cw + 6, ch + 6);
                                recordCtx.drawImage(remoteVideoEl, cx, cy, cw, ch);
                            }
                        } else {
                            recordCtx.fillStyle = '#000';
                            recordCtx.fillRect(0, 0, recordCanvas.width, recordCanvas.height);
                            const halfW = recordCanvas.width / 2;
                            // Слева — первый собеседник, справа — вы (тот же кадр, что уходит в звонок).
                            if (remoteVideoEl && remoteVideoEl.videoWidth) recordCtx.drawImage(remoteVideoEl, 0, 0, halfW, recordCanvas.height);
                            if (localVideo.videoWidth) recordCtx.drawImage(localVideo, halfW, 0, halfW, recordCanvas.height);
                        }
                        recordRafId = requestAnimationFrame(drawRecordFrame);
                    };
                    drawRecordFrame();

                    const videoTrack = recordCanvas.captureStream(30).getVideoTracks()[0];
                    const combined = new MediaStream([videoTrack, ...dest.stream.getAudioTracks()]);

                    const mimeCandidates = ['video/webm;codecs=vp9,opus', 'video/webm;codecs=vp8,opus', 'video/webm'];
                    const mimeType = mimeCandidates.find(m => window.MediaRecorder && MediaRecorder.isTypeSupported(m)) || '';

                    recordedChunks = [];
                    mediaRecorder = new MediaRecorder(combined, mimeType ? { mimeType } : undefined);
                    mediaRecorder.ondataavailable = (e) => { if (e.data && e.data.size > 0) recordedChunks.push(e.data); };
                    mediaRecorder.onstop = () => {
                        const blob = new Blob(recordedChunks, { type: 'video/webm' });
                        const url = URL.createObjectURL(blob);
                        const a = document.createElement('a');
                        const ts = new Date().toISOString().replace(/[:.]/g, '-');
                        a.href = url;
                        a.download = "storm-talk-" + ts + ".webm";
                        document.body.appendChild(a);
                        a.click();
                        a.remove();
                        setTimeout(() => URL.revokeObjectURL(url), 10000);
                        console.log("[TALK] Запись звонка сохранена: storm-talk-" + ts + ".webm");
                    };
                    mediaRecorder.start(1000);
                    isRecording = true;
                    recordBtn.classList.add('active');
                    recordBtn.style.color = '#ff5f5f';
                    recordBtn.title = "Остановить запись";
                    console.log("[TALK] Запись звонка начата.");
                } catch (e) {
                    console.error("[TALK] Не удалось начать запись: " + e.message);
                    showAlert("Не удалось начать запись: " + e.message);
                    isRecording = false;
                }
            }

            function stopRecording() {
                isRecording = false;
                recordBtn.classList.remove('active');
                recordBtn.style.color = '';
                recordBtn.title = "Запись звонка на диск";
                if (mediaRecorder && mediaRecorder.state !== 'inactive') mediaRecorder.stop();
                if (recordRafId) { cancelAnimationFrame(recordRafId); recordRafId = null; }
                if (recordAudioCtx) { recordAudioCtx.close(); recordAudioCtx = null; }
            }

            console.log("[TALK] Страница видеозвонка загружена.");
        </script>
    </body>
    </html>
    )HTML";
}