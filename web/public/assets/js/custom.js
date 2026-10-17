/**
* Updates and displays the current Server Time (configurable timezone)
* and the User's Local Time every second.
*/

document.addEventListener('DOMContentLoaded', () => {
  const serverTimeEl = document.getElementById('server-time');
  const userTimeEl = document.getElementById('user-time');
  const footerServerTimeEl = document.getElementById('footer-server-time');
  const serverTimeZone = serverTimeEl?.dataset?.tz;
  const fallbackTimeZone = Intl.DateTimeFormat().resolvedOptions().timeZone;
  const serverFormatter = new Intl.DateTimeFormat('es-ES', {
    timeZone: serverTimeZone || fallbackTimeZone,
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    hour12: false
  });

  const pad = n => String(n).padStart(2, '0');
  const formatLocalTime = date => `${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`;

  const updateTimes = () => {
    const now = new Date();
    if (serverTimeEl) serverTimeEl.textContent = serverFormatter.format(now);
    if (footerServerTimeEl) footerServerTimeEl.textContent = serverFormatter.format(now);
    if (userTimeEl) userTimeEl.textContent = formatLocalTime(now);
  };

  updateTimes();
  if (serverTimeEl || footerServerTimeEl || userTimeEl) {
    setInterval(updateTimes, 1000);
  }

  if (window.lucide) window.lucide.createIcons();

  const classStage = document.querySelector('[data-class-stage]');
  if (classStage) {
    const buttons = Array.from(classStage.querySelectorAll('[role="tab"]'));
    const image = classStage.querySelector('[data-class-image]');
    const name = classStage.querySelector('[data-class-name]');
    const role = classStage.querySelector('[data-class-role]');
    const copy = classStage.querySelector('[data-class-copy]');
    const number = classStage.querySelector('[data-class-number]');
    const traits = classStage.querySelector('[data-class-traits]');
    const cta = classStage.querySelector('[data-class-cta]');
    const selectClass = (button) => {
      buttons.forEach((item) => {
        const selected = item === button;
        item.classList.toggle('active', selected);
        item.setAttribute('aria-selected', String(selected));
      });
      image.style.opacity = '0';
      image.style.transform = 'translateY(8px)';
      window.setTimeout(() => {
        image.src = button.dataset.image;
        image.alt = button.dataset.name.replace(/\b\w/g, (letter) => letter.toUpperCase());
        name.textContent = button.dataset.name;
        role.textContent = button.dataset.role;
        copy.textContent = button.dataset.copy;
        number.textContent = button.dataset.number;
        cta.textContent = `BEGIN AS ${button.dataset.name}`;
        traits.innerHTML = button.dataset.traits.split('|').map((trait) => `<span>${trait}</span>`).join('');
        image.style.opacity = '1';
        image.style.transform = 'translateY(0)';
      }, 160);
    };
    buttons.forEach((button, index) => {
      button.addEventListener('click', () => selectClass(button));
      button.addEventListener('keydown', (event) => {
        if (event.key !== 'ArrowRight' && event.key !== 'ArrowLeft') return;
        event.preventDefault();
        const step = event.key === 'ArrowRight' ? 1 : -1;
        const next = buttons[(index + step + buttons.length) % buttons.length];
        next.focus(); selectClass(next);
      });
    });
  }

  const countdownEls = Array.from(document.querySelectorAll('[data-countdown]'));
  const formatCountdown = (totalSeconds) => {
    if (totalSeconds <= 0) return 'En curso';
    const hours = Math.floor(totalSeconds / 3600);
    const minutes = Math.floor((totalSeconds % 3600) / 60);
    const seconds = totalSeconds % 60;
    return `${pad(hours)}:${pad(minutes)}:${pad(seconds)}`;
  };

  const updateCountdowns = () => {
    if (countdownEls.length === 0) return;
    const now = Date.now();
    countdownEls.forEach((el) => {
      const target = Date.parse(el.getAttribute('data-countdown'));
      if (Number.isNaN(target)) return;
      const diffSeconds = Math.floor((target - now) / 1000);
      el.textContent = formatCountdown(diffSeconds);
    });
  };

  updateCountdowns();
  if (countdownEls.length > 0) {
    setInterval(updateCountdowns, 1000);
  }
});
