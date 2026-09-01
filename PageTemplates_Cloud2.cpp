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
// PageTemplates_Cloud2.cpp — storm://cloud, часть 2 из 2
// Вторая половина текста шаблона Storm Cloud (продолжение части 1 из
// PageTemplates_Cloud.cpp). См. шапку того файла — здесь только сам
// фрагмент текста, один в один как был в оригинале, без изменений.
// ==========================================================================

QString buildStormCloudHtmlPart2() {
    return QString(u8R"HTML(

    bridge.pingResult.connect(function(online) {
        var dot = document.getElementById('status-dot');
        var txt = document.getElementById('status-text');
        if (online) { dot.style.color = '#2ecc71'; txt.textContent = 'В сети'; }
        else { dot.style.color = '#ff5f5f'; txt.textContent = 'Нет связи'; }
    });

    bridge.billingInfoReceived.connect(function(balance, tokens, isPremium, premiumUntil,
                                                 invitesLeft, inviteCode, vpnActive, vpnTrafficMb, email, purchaseHistory,
                                                 premiumTier, vpnLimitMb) {
        lastBalance = balance + " SC";
        lastBalanceRaw = balance;
        lastTokens = String(tokens);
        document.getElementById('lbl-sc').textContent = lastBalance;
        document.getElementById('lbl-ai').textContent = "🧠 " + tokens + " Токенов";

        isPremiumUser = isPremium;
        currentPremiumTier = premiumTier || '';
        var premLbl = document.getElementById('lbl-prem');
        var premDateLbl = document.getElementById('lbl-prem-date');
        var premPill = document.getElementById('prem-pill');
        var renewBtn = document.getElementById('prem-renew-btn');
        if (isPremium) {
            premLbl.textContent = '👑 Premium';
            premDateLbl.textContent = 'до ' + (premiumUntil ? premiumUntil.substring(0, 10) : 'бессрочно');
            premDateLbl.style.display = 'block';
            premPill.classList.add('is-premium');
            document.getElementById('user-name-label').textContent =
                document.getElementById('user-name-label').textContent.replace(' 👑','') + ' 👑';
            applyPremiumVisuals();

            // Кнопка "Продлить" — только когда подписка истекает в ближайшие 7 дней,
            // чтобы не занимать место в пилюле весь остальной срок.
            var daysLeft = premiumUntil ? Math.ceil((new Date(premiumUntil.replace(' ', 'T')) - new Date()) / 86400000) : 999;
            if (daysLeft <= 7) {
                renewBtn.style.display = 'inline-block';
                renewBtn.textContent = daysLeft <= 0 ? 'Продлить' : ('Продлить (' + daysLeft + 'д)');
            } else {
                renewBtn.style.display = 'none';
            }
        } else {
            premLbl.textContent = 'Стандарт';
            premDateLbl.style.display = 'none';
            premPill.classList.remove('is-premium');
            renewBtn.style.display = 'none';
        }

        document.getElementById('ref-header').innerHTML =
            "🎁 <b>Пригласи друга (Осталось инвайтов: " + invitesLeft + "/3)</b>";
        document.getElementById('ref-code').innerHTML = (invitesLeft > 0 && inviteCode)
            ? "Твой код для друга: <b>" + inviteCode + "</b>"
            : "Вы исчерпали лимит в 3 приглашения. Спасибо!";

        // vpnLimitMb приходит от сервера динамически (15000 на тарифах 6/12 мес,
        // иначе 5000) — до обновления моста параметр не придёт, тогда считаем 5000.
        var limitMb = vpnLimitMb || 5000;
        var pct = Math.min(100, (vpnTrafficMb / limitMb) * 100);
        document.getElementById('vpn-progress-fill').style.width = pct + '%';
        document.getElementById('vpn-traffic').textContent =
            "Трафик: " + vpnTrafficMb.toFixed(1) + " МБ / " + limitMb + " МБ";

        var vpnBtn = document.getElementById('btn-buy-vpn');
        if (vpnActive) {
            document.getElementById('vpn-status').textContent = "Статус: Доступен (Подписка Активна) 🔑";
            vpnBtn.textContent = "Включить Storm VPN ⚡";
        } else {
            document.getElementById('vpn-status').textContent = "Статус: Не активен 🔴";
            vpnBtn.textContent = "🛍️ Купить VPN в Магазине";
        }

        document.getElementById('store-balance').textContent = lastBalance;
        document.getElementById('store-tokens').textContent = lastTokens;
        document.getElementById('wallet-balance').textContent = lastBalance;
        renderWalletPremiumBlock();
        updateEmailStatus(email);
        updateAffordability();
        renderPremiumManageBox(isPremium, premiumUntil);
        renderPurchaseHistory(purchaseHistory);
    });

    bridge.setEmailFinished.connect(function(success, message) {
        showToast(message);
        if (success) bridge.fetchBillingInfo(); // перечитать email и обновить статус в карточке
    });

    bridge.forgotPasswordFinished.connect(function(success, message) {
        document.getElementById('forgot-message').textContent = message;
    });

    bridge.purchaseFinished.connect(function(success, message) {
        showToast(message);
        if (success) {
            closeModal('store-modal');
            bridge.fetchBillingInfo();
            if (bridge.fetchResearchQuota) bridge.fetchResearchQuota(); // на случай покупки одного из pack_research_*
        }
    });

    bridge.betaFinished.connect(function(success, message) {
        var btn = document.getElementById('beta-btn');
        if (success) {
            btn.textContent = '✅ Заявка на рассмотрении';
        } else {
            btn.disabled = false;
            btn.textContent = '🚀 Подать заявку на Beta-тест';
        }
        showToast(message);
    });

    bridge.notImplementedYet.connect(function(feature) {
        showToast('⏳ ' + feature + ' пока в разработке.');
    });
 
    bridge.vpnStateChanged.connect(function(connected, message) {
        vpnConnected = connected;
        var btn = document.getElementById('btn-buy-vpn');
        btn.disabled = false;
        btn.textContent = connected ? 'Выключить Storm VPN 🛑' : 'Включить Storm VPN ⚡';
        document.getElementById('vpn-status').textContent =
            connected ? 'Статус: Соединение установлено 🟢' : 'Статус: Отключен 🔴';
        showToast(message);
    });
 
    bridge.cloudClearFinished.connect(function(success, message) {
        showToast(message);
    });
 
    bridge.syncFinished.connect(function(success, message) {
        var btn = document.getElementById('sync-now-btn');
        btn.disabled = false;
        btn.textContent = '🔄 Синхронизировать сейчас';
        if (success) {
            document.getElementById('last-sync-label').textContent = 'Синхронизация: ' + message;
            showToast('✅ Синхронизация завершена');
            bridge.fetchLocalStats();
        } else {
            showToast('❌ ' + message);
        }
    });
 
    bridge.localStatsReceived.connect(function(historyCount, passwordCount, bookmarkCount) {
        var cards = document.querySelectorAll('.stat-card .v');
        if (cards[0]) cards[0].textContent = historyCount;
        if (cards[1]) cards[1].textContent = passwordCount;
        if (cards[2]) cards[2].textContent = bookmarkCount;
    });

    bridge.friendsReceived.connect(function(added, referred, addedMe) {
        renderFriendsList(added, referred, addedMe);
    });

    bridge.friendActionFinished.connect(function(success, message) {
        showToast(message);
        if (success) document.getElementById('friend-input').value = '';
    });

    // ---- Хаб настроек: устройства, смена пароля, уведомления ----
    // Подключаем защищённо — до обновления StormCloudBridge этих сигналов
    // ещё нет на C++-стороне, и страница не должна падать без них.
    if (bridge.loginHistoryReceived) {
        bridge.loginHistoryReceived.connect(function(events) {
            renderDevicesList(events);
        });
    }
    if (bridge.changePasswordFinished) {
        bridge.changePasswordFinished.connect(function(success, message) {
            showToast(message);
            if (success) {
                document.getElementById('settings-old-password').value = '';
                document.getElementById('settings-new-password').value = '';
            }
        });
    }
    if (bridge.notificationPrefsReceived) {
        bridge.notificationPrefsReceived.connect(function(notifyNewDevice, notifyPurchases, notifyPremiumExpiry) {
            setPrefToggleState('new_device', notifyNewDevice);
            setPrefToggleState('purchases', notifyPurchases);
            setPrefToggleState('premium_expiry', notifyPremiumExpiry);
        });
    }
    if (bridge.notificationPrefsSaved) {
        bridge.notificationPrefsSaved.connect(function(success, message) {
            showToast(message);
        });
    }
    if (bridge.giftPremiumFinished) {
        bridge.giftPremiumFinished.connect(function(success, message) {
            showToast(message);
            if (success) {
                cancelGiftMode();
                closeModal('store-modal');
                call('fetchFriends'); // подтянуть корону у одаренного друга
                call('fetchBillingInfo'); // обновить свой баланс после списания
            }
        });
    }
    // Квота "🔬 Глубокий анализ" — защищённое подключение, как и хаб настроек
    // выше: до обновления StormCloudBridge этого сигнала может не быть.
    if (bridge.researchQuotaReceived) {
        bridge.researchQuotaReceived.connect(function(used, limit, tier, resetsAt) {
            renderResearchQuotaStatus(used, limit, tier, resetsAt);
        });
    }
}

function showManageScreen() {
    document.getElementById('screen-login').style.display = 'none';
    document.getElementById('screen-manage').style.display = 'block';
    renderAvatar();
}

function doLogout() {
    call('logout');
    document.getElementById('screen-manage').style.display = 'none';
    document.getElementById('screen-login').style.display = 'flex';
    document.getElementById('login-password').value = '';
}

// ---------------- АВАТАР ----------------
function renderAvatar() {
    var btn = document.getElementById('avatar-btn');
    if (avatarAnimTimer) { clearInterval(avatarAnimTimer); avatarAnimTimer = null; }
    if (savedAvatar.indexOf('data:image') === 0) {
        btn.innerHTML = '<img src="' + savedAvatar + '">';
    } else {
        btn.textContent = savedAvatar;
    }
}

function buildAvatarGrid() {
    var grid = document.getElementById('avatar-grid');
    grid.innerHTML = '';
    AVATAR_EMOJIS.forEach(function(e) {
        var d = document.createElement('div');
        d.className = 'avatar-opt';
        d.textContent = e;
        d.onclick = function() {
            savedAvatar = e;
            call('saveAvatar', e);
            renderAvatar();
            closeModal('avatar-modal');
        };
        grid.appendChild(d);
    });
}
buildAvatarGrid();

document.getElementById('avatar-file-input').addEventListener('change', function(e) {
    var file = e.target.files[0];
    if (!file) return;
    var reader = new FileReader();
    reader.onload = function(ev) {
        savedAvatar = ev.target.result;
        call('saveAvatar', savedAvatar);
        renderAvatar();
        closeModal('avatar-modal');
    };
    reader.readAsDataURL(file);
});

// ---------------- ПРЕМИУМ-ТЕМЫ ----------------
// ---------------- ХАБ НАСТРОЕК (шестерёнка) ----------------
var SETTINGS_TABS = ['account', 'devices', 'notify', 'appearance'];

function openSettingsModal() {
    renderSettingsAppearanceTab();
    switchSettingsTab('account');
    openModal('settings-modal');
    call('fetchLoginHistory');
    call('fetchNotificationPrefs');
}

function switchSettingsTab(tab) {
    SETTINGS_TABS.forEach(function(t) {
        document.getElementById('settings-tab-panel-' + t).style.display = (t === tab) ? 'block' : 'none';
        document.getElementById('settings-tab-btn-' + t).classList.toggle('active', t === tab);
    });
}

function renderSettingsAppearanceTab() {
    var content = document.getElementById('appearance-tab-content');
    if (!isPremiumUser) {
        content.innerHTML = '<div class="premium-locked">👑 Кастомизация оформления (фон, рамка аватара, ' +
            'анимированные аватарки) доступна только на подписке Storm Premium.</div>';
        return;
    }
    var bgOpts = Object.keys(BG_THEMES).map(function(k) {
        return '<option value="'+k+'"'+(k===savedBg?' selected':'')+'>'+k+'</option>';
    }).join('');
    var frameOpts = Object.keys(FRAME_COLORS).map(function(k) {
        return '<option value="'+k+'"'+(k===savedFrame?' selected':'')+'>'+k+'</option>';
    }).join('');
    var avatarOpts = Object.keys(AVATAR_FRAMES).map(function(k) {
        return '<option value="'+k+'"'+(k===savedAvatarFx?' selected':'')+'>'+k+'</option>';
    }).join('');

    content.innerHTML =
        '<div class="theme-select-row"><label>Фон Личного Кабинета</label>' +
        '<select id="sel-bg">'+bgOpts+'</select></div>' +
        '<div class="theme-select-row"><label>Рамка аватара</label>' +
        '<select id="sel-frame">'+frameOpts+'</select></div>' +
        '<div class="theme-select-row"><label>Анимированный аватар</label>' +
        '<select id="sel-avatarfx">'+avatarOpts+'</select></div>' +
        '<button class="btn btn-purple" onclick="savePremiumSelections()">💾 Сохранить оформление</button>';
}

function savePremiumSelections() {
    savedBg = document.getElementById('sel-bg').value;
    savedFrame = document.getElementById('sel-frame').value;
    savedAvatarFx = document.getElementById('sel-avatarfx').value;
    call('savePremiumTheme', savedBg, savedFrame, savedAvatarFx);
    applyPremiumVisuals();
    showToast('Оформление сохранено!');
}

// ---- Устройства и входы ----
function renderDevicesList(events) {
    var el = document.getElementById('devices-list');
    if (!events || events.length === 0) {
        el.innerHTML = '<div class="settings-hint" style="margin-top:0;">Пока нет записей о входах.</div>';
        return;
    }
    var html = '';
    events.forEach(function(e) {
        var when = (e.ts || '').substring(0, 16).replace('T', ' ');
        // device_label — новое поле (хост + ОС). У старых записей, сделанных до
        // обновления моста, его нет — тогда как раньше показываем кусок hwid,
        // чтобы строка не была пустой.
        var deviceName = e.device_label || (e.hwid ? ('устройство ' + e.hwid.substring(0, 8)) : 'устройство неизвестно');
        html += '<div class="device-row">' +
            '<div><div class="device-main">' + escapeHtml(deviceName) + '</div>' +
            '<div class="device-meta">' + escapeHtml(e.ip || 'IP неизвестен') + ' · ' + escapeHtml(when) + '</div></div>' +
            '<button class="device-flag-btn" onclick="flagUnknownDevice()">Это не я</button>' +
            '</div>';
    });
    el.innerHTML = html;
}

function flagUnknownDevice() {
    switchSettingsTab('account');
    showToast('Если это были не вы — смените пароль ниже.');
}

// ---- Смена пароля ----
function doChangePassword() {
    var oldPw = document.getElementById('settings-old-password').value;
    var newPw = document.getElementById('settings-new-password').value;
    if (!oldPw || !newPw) { showToast('Заполните оба поля'); return; }
    if (newPw.length < 6) { showToast('Новый пароль — минимум 6 символов'); return; }
    call('changePassword', oldPw, newPw);
}

// ---- Уведомления ----
var notifyPrefsState = { new_device: true, purchases: true, premium_expiry: true };

function setPrefToggleState(key, value) {
    notifyPrefsState[key] = value;
    var btn = document.getElementById('notify-' + key.replace(/_/g, '-') + '-btn');
    if (!btn) return;
    btn.classList.toggle('active', value);
    btn.querySelector('.as-state').textContent = value ? 'вкл' : 'выкл';
}

function toggleNotifyPref(key) {
    setPrefToggleState(key, !notifyPrefsState[key]);
    call('saveNotificationPrefs', notifyPrefsState.new_device, notifyPrefsState.purchases, notifyPrefsState.premium_expiry);
}

function applyPremiumVisuals() {
    if (!isPremiumUser) return;

    var container = document.getElementById('cloud-container');
    var bgGrad = BG_THEMES[savedBg];
    container.style.background = bgGrad ? bgGrad : '#0f172a';

    var avatarBtn = document.getElementById('avatar-btn');
    var frameColor = FRAME_COLORS[savedFrame];
    avatarBtn.style.border = frameColor ? ('3px solid ' + frameColor) : 'none';

    if (avatarAnimTimer) { clearInterval(avatarAnimTimer); avatarAnimTimer = null; }
    var frames = AVATAR_FRAMES[savedAvatarFx];
    if (frames) {
        var idx = 0;
        avatarBtn.textContent = frames[0];
        avatarAnimTimer = setInterval(function() {
            idx = (idx + 1) % frames.length;
            avatarBtn.textContent = frames[idx];
        }, 450);
    }
}

// ---------------- МАГАЗИН / КОШЕЛЁК ----------------
function doPurchase(packageId, itemName, price) {
    // Подтверждение перед списанием — раньше клик по кнопке сразу тратил SC.
    if (!confirm('Купить «' + itemName + '» за ' + price + ' SC?')) return;
    call('purchase', packageId);
}

// ---------------- ВИТРИНА: вкладки ----------------
function switchStoreTab(tab) {
    ['tokens', 'vpn', 'premium', 'research'].forEach(function(t) {
        document.getElementById('store-tab-panel-' + t).style.display = (t === tab) ? 'block' : 'none';
        document.getElementById('store-tab-btn-' + t).classList.toggle('active', t === tab);
    });
}

function openStoreForRenewal() {
    openModal('store-modal');
    switchStoreTab('premium');
}

// ---------------- ВИТРИНА: подсказка "не хватает SC" ----------------
var STORE_PRODUCT_PRICES = {
    pack_20k: 150, pack_50k: 299, pack_100k: 590, pack_vpn_5gb: 390,
    pack_premium_1m: 149, pack_premium_6m: 790, pack_premium_1y: 1490,
    pack_research_10: 149, pack_research_20: 249, pack_research_30: 349
};
function updateAffordability() {
    Object.keys(STORE_PRODUCT_PRICES).forEach(function(id) {
        var btn = document.getElementById('buy-btn-' + id);
        if (!btn) return;
        var shortfall = STORE_PRODUCT_PRICES[id] - lastBalanceRaw;
        btn.classList.toggle('cant-afford', shortfall > 0);
        var note = document.getElementById('afford-note-' + id);
        if (note) {
            if (shortfall > 0) {
                note.textContent = 'Не хватает ' + shortfall + ' SC';
                note.style.display = 'block';
            } else {
                note.style.display = 'none';
            }
        }
    });
}

// ---------------- ВИТРИНА: статус квоты "Глубокий анализ" ----------------
var RESEARCH_TIER_LABELS = {
    free: 'Бесплатный', pack_research_10: '10 отчётов/30д', pack_research_20: '20 отчётов/30д', pack_research_30: '30 отчётов/30д'
};
function renderResearchQuotaStatus(used, limit, tier, resetsAt) {
    var el = document.getElementById('research-quota-status');
    if (!el) return;
    var parts = [];
    parts.push('Тариф: <b style="color:#a371f7">' + (RESEARCH_TIER_LABELS[tier] || tier) + '</b>');
    if (used >= 0 && limit >= 0) {
        parts.push('использовано <b style="color:#eef3ff">' + used + ' / ' + limit + '</b>');
    }
    if (resetsAt) {
        parts.push('обновление ' + resetsAt);
    }
    el.innerHTML = parts.join(' · ');
}

// ---------------- КОШЕЛЁК: история покупок ----------------
function renderPurchaseHistory(history) {
    var el = document.getElementById('purchase-history-list');
    if (!history || !history.length) {
        el.innerHTML = '<div style="opacity:.6;">Покупок пока не было</div>';
        return;
    }
    var html = '';
    history.forEach(function(h) {
        var date = h.purchase_date ? String(h.purchase_date).substring(0, 16) : '';
        html += '<div style="display:flex; justify-content:space-between; gap:8px; padding:4px 0; border-bottom:1px solid #ffffff0d;">' +
            '<span>' + escapeHtml(h.item_name || '') + '</span>' +
            '<span style="color:#94a3b8; white-space:nowrap;">' + h.price + ' SC</span></div>' +
            (date ? '<div style="color:#64748b; font-size:10px; margin-bottom:4px;">' + date + '</div>' : '');
    });
    el.innerHTML = html;
}

// ---------------- ВИТРИНА: управление подпиской Premium ----------------
function renderPremiumManageBox(isPremium, premiumUntil) {
    var box = document.getElementById('premium-manage-box');
    if (!box) return;
    if (isPremium) {
        var untilDate = premiumUntil ? String(premiumUntil).substring(0, 10) : 'не указана';
        box.innerHTML =
            '<div class="premium-active-box">' +
            '<div style="font-size:15px;">👑 <b>Storm Premium активен</b></div>' +
            '<div style="color:#94a3b8; font-size:12px; margin-top:4px;">Действует до: ' + untilDate + '</div>' +
            '</div>';
    } else {
        box.innerHTML = '';
    }
}

function renderWalletPremiumBlock() {
    var block = document.getElementById('wallet-premium-block');
    if (isPremiumUser) {
        block.innerHTML = '<h3 style="color:#ffd700;text-align:center;">👑 Storm Premium (Активирован)</h3>';
    } else {
        block.innerHTML = '';
    }
}

// ---------------- VPN ----------------
var vpnConnected = false;
 
function handleVpnClick() {
    var btn = document.getElementById('btn-buy-vpn');
    if (btn.textContent.indexOf('Купить') !== -1) {
        openModal('store-modal');
        return;
    }
    btn.disabled = true;
    if (!vpnConnected) {
        btn.textContent = '⏳ Подключение...';
        call('startVpn');
    } else {
        btn.textContent = '⏳ Отключение...';
        call('stopVpn');
    }
}
 
function confirmClearCloud() {
    if (confirm('Точно удалить данные из облака?')) {
        call('clearCloudData');
    }
}
 
function runSync() {
    var btn = document.getElementById('sync-now-btn');
    btn.disabled = true;
    btn.textContent = '⏳ Идёт синхронизация...';
    call('requestManualSync');
}

// ---------------- BETA ----------------
function requestBetaAccess() {
    var btn = document.getElementById('beta-btn');
    btn.disabled = true;
    btn.textContent = '⏳ Отправка заявки...';
    call('requestBeta');
}

function showBetaInfo() {
    alert(
        "Добро пожаловать в Экосистему Storm! 🌪️\n\n" +
        "Как работает закрытая экономика браузера:\n" +
        "1. Storm Coins (SC) — это наша внутренняя валюта для активных участников.\n" +
        "2. SC используются для покупки AI-токенов, которые нужны для работы умной нейросети.\n" +
        "3. Чтобы получить стартовый капитал SC бесплатно, нажмите кнопку «Подать заявку на Beta-тест».\n\n" +
        "⚠️ ВАЖНОЕ УСЛОВИЕ:\n" +
        "Все заявки проверяются вручную. Начисление тестовых SC происходит в течение 24 часов!\n\n" +
        "4. Как только SC поступят на ваш баланс, вы сможете обменять их на AI-токены в Магазине."
    );
}

// ---------------- EMAIL ----------------
function updateEmailStatus(email) {
    var el = document.getElementById('email-status');
    var input = document.getElementById('account-email-input');
    if (email) {
        el.textContent = 'Привязан: ' + email + ' — используется для восстановления пароля и уведомлений о входе.';
        input.placeholder = email;
    } else {
        el.textContent = 'Email не указан — без него не сработает "Забыли пароль?" и уведомления о новом устройстве.';
        el.style.color = '#f59e0b';
    }
}
function doSaveEmail() {
    var v = document.getElementById('account-email-input').value.trim();
    if (!v) return;
    call('setEmail', v);
    document.getElementById('account-email-input').value = '';
}

// ---------------- ДРУЗЬЯ ----------------
var friendsData = { added: [], referred: [], addedMe: [] };
var friendsFilter = 'all';
var friendsVisible = 12;
var FRIENDS_PAGE_SIZE = 12;

var FRIENDS_FILTERS = [
    { id: 'all', label: 'Все' },
    { id: 'added', label: 'Добавленные' },
    { id: 'referred', label: 'Рефералы' },
    { id: 'addedMe', label: 'Добавили вас' }
];

function doAddFriend() {
    var input = document.getElementById('friend-input');
    var name = input.value.trim();
    if (!name) return;
    call('addFriend', name);
}

function doRemoveFriend(name) {
    call('removeFriend', name);
}

function doAddFriendByName(name) {
    if (!name) return;
    call('addFriend', name);
}

function escapeHtml(str) {
    var d = document.createElement('div');
    d.textContent = str;
    return d.innerHTML;
}

function friendInitial(name) {
    var m = (name || '').match(/[a-zA-Zа-яА-Я0-9]/);
    return (m ? m[0] : '?').toUpperCase();
}

function friendColor(name) {
    var ramp = ['#a371f7', '#4facfe', '#4fd6a0', '#f472b6', '#f5a524'];
    var h = 0;
    for (var i = 0; i < name.length; i++) h = (h * 31 + name.charCodeAt(i)) >>> 0;
    return ramp[h % ramp.length];
}

// Вызывается по сигналу bridge.friendsReceived — кэшируем данные и сбрасываем
// поиск/фильтр/пагинацию к дефолту (это "свежий" снимок списка).
function renderFriendsList(added, referred, addedMe) {
    friendsData.added = added || [];
    friendsData.referred = referred || [];
    friendsData.addedMe = addedMe || [];
    friendsFilter = 'all';
    friendsVisible = FRIENDS_PAGE_SIZE;
    var search = document.getElementById('friend-search');
    if (search) search.value = '';
    refreshFriendsUI();
}

function friendsCounts() {
    // "Все" — это количество РЕАЛЬНЫХ людей, а не сумма записей по категориям:
    // один и тот же человек может быть и добавлен вами, и по рефералке, и
    // добавить вас — считаем его один раз, как и в getFriendRows() ниже.
    var unique = {};
    friendsData.added.forEach(function(f) { unique[f.friend_username] = true; });
    friendsData.referred.forEach(function(f) { unique[f.friend_username] = true; });
    friendsData.addedMe.forEach(function(f) { unique[f.friend_username] = true; });
    return {
        all: Object.keys(unique).length,
        added: friendsData.added.length,
        referred: friendsData.referred.length,
        addedMe: friendsData.addedMe.length
    };
}

function renderFriendFilterPills() {
    var counts = friendsCounts();
    var el = document.getElementById('filter-pills');
    el.innerHTML = '';
    FRIENDS_FILTERS.forEach(function(f) {
        var b = document.createElement('button');
        b.type = 'button';
        b.className = 'filter-pill' + (friendsFilter === f.id ? ' active' : '');
        b.textContent = f.label + ' ' + counts[f.id];
        b.onclick = function() {
            friendsFilter = f.id;
            friendsVisible = FRIENDS_PAGE_SIZE;
            renderFriendFilterPills();
            renderFriendRows();
        };
        el.appendChild(b);
    });
    var badge = document.getElementById('friends-count-badge');
    if (badge) badge.textContent = counts.all;
}

// Собирает плоский список строк под текущий фильтр + поисковый запрос.
// Во вкладке "Все" один человек может быть добавлен вручную И по рефералке
// И добавить вас сам — схлопываем это в ОДНУ строку с объединённой меткой,
// а не показываем одного и того же друга несколько раз подряд.
function getFriendRows() {
    var addedSet = {};
    friendsData.added.forEach(function(f) { addedSet[f.friend_username] = true; });

    var rows;

    if (friendsFilter === 'all') {
        var byName = {};
        var order = [];
        function upsert(name, tagPart, isPremium) {
            if (!byName[name]) {
                byName[name] = { name: name, tags: [], isPremium: false, kinds: {} };
                order.push(name);
            }
            if (byName[name].tags.indexOf(tagPart) === -1) byName[name].tags.push(tagPart);
            if (isPremium) byName[name].isPremium = true;
        }

        friendsData.added.forEach(function(f) {
            var name = f.friend_username || '';
            upsert(name, 'Добавлен вами', f.is_premium);
            byName[name].kinds.added = true;
        });
        friendsData.referred.forEach(function(f) {
            var name = f.friend_username || '';
            upsert(name, 'По рефералке', f.is_premium);
            byName[name].kinds.referred = true;
        });
        friendsData.addedMe.forEach(function(f) {
            var name = f.friend_username || '';
            var mutual = !!addedSet[name];
            upsert(name, mutual ? 'Взаимно' : 'Добавил(а) вас', f.is_premium);
            byName[name].kinds.addedMe = true;
            byName[name].mutual = mutual;
        });

        rows = order.map(function(name) {
            var e = byName[name];
            // Кнопка действия приоритет: если друг реально в "добавленных" —
            // показываем "удалить", иначе если только добавил вас — "добавить в ответ".
            var kind = e.kinds.added ? 'added' : ((e.kinds.addedMe && !e.mutual) ? 'addedMe' : 'referred');
            return { name: name, tag: e.tags.join(' · '), kind: kind, mutual: e.mutual, isPremium: e.isPremium };
        });
    } else if (friendsFilter === 'added') {
        rows = friendsData.added.map(function(f) {
            return { name: f.friend_username || '', tag: 'Добавлен вами', kind: 'added', isPremium: !!f.is_premium };
        });
    } else if (friendsFilter === 'referred') {
        rows = friendsData.referred.map(function(f) {
            return { name: f.friend_username || '', tag: 'По рефералке', kind: 'referred', isPremium: !!f.is_premium };
        });
    } else { // addedMe
        rows = friendsData.addedMe.map(function(f) {
            var name = f.friend_username || '';
            var mutual = !!addedSet[name];
            return { name: name, tag: mutual ? 'Взаимно' : 'Добавил(а) вас', kind: 'addedMe', mutual: mutual, isPremium: !!f.is_premium };
        });
    }

    var query = document.getElementById('friend-search').value.trim().toLowerCase();
    if (query) {
        rows = rows.filter(function(r) { return r.name.toLowerCase().indexOf(query) !== -1; });
    }
    return rows;
}

// Рендерит только видимый срез (friendsVisible), остальное — по кнопке "Показать ещё".
// Именно это не даёт списку "расползтись" при большом количестве друзей.
function renderFriendRows() {
    var el = document.getElementById('friends-list');
    var loadBtn = document.getElementById('load-more-btn');
    var rows = getFriendRows();

    if (friendsCounts().all === 0) {
        el.innerHTML = '<div class="friends-empty">Пока никого нет — добавьте друга по логину или пригласите по рефкоду выше.</div>';
        loadBtn.style.display = 'none';
        return;
    }
    if (rows.length === 0) {
        el.innerHTML = '<div class="friends-empty">Никого не найдено по этому фильтру или запросу.</div>';
        loadBtn.style.display = 'none';
        return;
    }

    var slice = rows.slice(0, friendsVisible);
    var html = '';
    slice.forEach(function(r) {
        var name = escapeHtml(r.name);
        var safeName = r.name.replace(/'/g, "\\'");
        var action = '';
        if (r.kind === 'added') {
            action = '<button class="fbtn remove" title="Удалить" onclick="doRemoveFriend(\'' + safeName + '\')">✕</button>';
        } else if (r.kind === 'addedMe' && !r.mutual) {
            action = '<button class="fbtn add" title="Добавить в ответ" onclick="doAddFriendByName(\'' + safeName + '\')">➕</button>';
        }
        var giftBtn = '<button class="fbtn gift" title="Подарить Premium" onclick="openGiftPremiumModal(\'' + safeName + '\')">🎁</button>';
        var crown = r.isPremium ? '<span class="fcrown" title="Storm Premium">👑</span>' : '';
        html += '<div class="frow">' +
            '<div class="favatar" style="background:' + friendColor(r.name) + '">' + friendInitial(r.name) + '</div>' +
            '<div class="fbody"><div class="fname">' + name + crown + '</div><div class="ftag">' + r.tag + '</div></div>' +
            '<div class="factions">' + giftBtn + action + '</div>' +
            '</div>';
    });
    el.innerHTML = html;

    if (rows.length > friendsVisible) {
        loadBtn.style.display = 'block';
        loadBtn.textContent = 'Показать ещё (' + (rows.length - friendsVisible) + ')';
    } else {
        loadBtn.style.display = 'none';
    }
}

function onLoadMoreFriends() {
    friendsVisible += FRIENDS_PAGE_SIZE;
    renderFriendRows();
}

function onFriendSearchInput() {
    friendsVisible = FRIENDS_PAGE_SIZE;
    renderFriendRows();
}

function refreshFriendsUI() {
    renderFriendFilterPills();
    renderFriendRows();
}

// ---------------- АВТО-СИНХРОНИЗАЦИЯ ----------------
var autoSyncEnabled = autoSyncSaved;
var autoSyncTimer = null;
var AUTO_SYNC_INTERVAL_MS = 5 * 60 * 1000; // раз в 5 минут, пока открыта вкладка storm://cloud

function updateAutoSyncBtn() {
    var btn = document.getElementById('auto-sync-btn');
    btn.innerHTML = '<span>🔄 Авто-синхронизация</span><span class="as-state">' + (autoSyncEnabled ? 'вкл' : 'выкл') + '</span>';
    btn.classList.toggle('active', autoSyncEnabled);

    if (autoSyncTimer) { clearInterval(autoSyncTimer); autoSyncTimer = null; }
    if (autoSyncEnabled) {
        autoSyncTimer = setInterval(function() {
            if (bridge && document.getElementById('screen-manage').style.display !== 'none') {
                call('requestManualSync');
            }
        }, AUTO_SYNC_INTERVAL_MS);
    }
}
function toggleAutoSync() {
    autoSyncEnabled = !autoSyncEnabled;
    call('setAutoSync', autoSyncEnabled);
    updateAutoSyncBtn();
}
updateAutoSyncBtn();
</script>
</body>
</html>
    )HTML");
}