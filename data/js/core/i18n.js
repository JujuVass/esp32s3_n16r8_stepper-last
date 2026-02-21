/**
 * ============================================================================
 * i18n.js - Internationalization Module
 * ============================================================================
 * Lightweight i18n system for ESP32 Stepper Controller
 * 
 * Features:
 * - JSON-based translation dictionaries (fr.json / en.json)
 * - t('key') function for JS dynamic strings
 * - t('key', {var: value}) for parameterized strings
 * - data-i18n attribute for HTML static elements
 * - data-i18n-title for title/tooltip attributes
 * - data-i18n-placeholder for input placeholders
 * - Language persisted in localStorage
 * - Flag selector toggle (🇫🇷/🇬🇧)
 * 
 * Usage:
 *   t('calibration.inProgress')           → "Calibration en cours..."
 *   t('speed.limited', {freq: 0.5})       → "Fréquence limitée: 0.5 Hz"
 *   <span data-i18n="status.ready">       → auto-translated on language change
 * 
 * Created: February 2026
 * ============================================================================
 */

const I18n = (() => {
  // Private state
  let _currentLang = 'fr';
  let _translations = {};
  let _loaded = false;
  const STORAGE_KEY = 'stepper_lang';
  const SUPPORTED_LANGS = ['fr', 'en'];

  /**
   * Initialize i18n system
   * Loads saved language preference and translation file
   */
  async function init() {
    // Restore saved language or default to 'fr'
    const saved = localStorage.getItem(STORAGE_KEY);
    if (saved && SUPPORTED_LANGS.includes(saved)) {
      _currentLang = saved;
    }
    
    // Load translation file
    await _loadTranslations(_currentLang);
    
    // Apply translations to existing DOM
    applyTranslations();
    
    // Update flag selector UI
    _updateFlagUI();
    
    _loaded = true;
    console.debug(`✅ i18n initialized: lang=${_currentLang}`);
  }

  /**
   * Load translation JSON file
   * @param {string} lang - Language code ('fr' or 'en')
   */
  async function _loadTranslations(lang) {
    try {
      const response = await fetch(`/lang/${lang}.json`);
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      _translations = await response.json();
    } catch (e) {
      console.error(`i18n: Failed to load /lang/${lang}.json:`, e);
      // Fallback: try to load French if English fails
      if (lang !== 'fr') {
        try {
          const fallback = await fetch('/lang/fr.json');
          if (fallback.ok) _translations = await fallback.json();
        } catch (error_) {
          console.error('i18n: Fallback to fr.json also failed:', error_.message);
        }
      }
    }
  }

  /**
   * Translate a key with optional parameters
   * @param {string} key - Dot-notation key (e.g. 'calibration.inProgress')
   * @param {Object} params - Optional parameters for interpolation
   * @returns {string} Translated string or key if not found
   */
  function t(key, params = null) {
    // Navigate nested keys
    const parts = key.split('.');
    let value = _translations;
    
    for (const part of parts) {
      if (value && typeof value === 'object' && part in value) {
        value = value[part];
      } else {
        // Key not found - return the key itself as fallback
        return key;
      }
    }
    
    // Allow strings and arrays (e.g. states[], dayNames[])
    if (typeof value !== 'string' && !Array.isArray(value)) return key;
    
    // Arrays are returned as-is (no parameter interpolation)
    if (Array.isArray(value)) return value;
    
    // Replace parameters: {{paramName}}
    if (params) {
      return value.replace(/\{\{(\w+)\}\}/g, (match, name) => {
        return params[name] === undefined ? match : params[name];
      });
    }
    
    return value;
  }

  /**
   * Switch language and re-apply all translations
   * @param {string} lang - Language code
   */
  async function setLanguage(lang) {
    if (!SUPPORTED_LANGS.includes(lang)) return;
    if (lang === _currentLang && _loaded) return;
    
    _currentLang = lang;
    localStorage.setItem(STORAGE_KEY, lang);
    
    await _loadTranslations(lang);
    applyTranslations();
    _updateFlagUI();
    
    console.debug(`🌐 Language switched to: ${lang}`);
  }

  /**
   * Toggle between available languages
   */
  async function toggleLanguage() {
    const nextIndex = (SUPPORTED_LANGS.indexOf(_currentLang) + 1) % SUPPORTED_LANGS.length;
    await setLanguage(SUPPORTED_LANGS[nextIndex]);
  }

  /**
   * Apply translations to all DOM elements with data-i18n attributes
   */
  function applyTranslations() {
    // Translate text content
    document.querySelectorAll('[data-i18n]').forEach(el => {
      const key = el.dataset.i18n;
      const translated = t(key);
      if (translated !== key) {
        el.textContent = translated;
      }
    });

    // Translate innerHTML (for elements with HTML content)
    document.querySelectorAll('[data-i18n-html]').forEach(el => {
      const key = el.dataset.i18nHtml;
      const translated = t(key);
      if (translated !== key) {
        el.innerHTML = translated;
      }
    });

    // Translate title/tooltip attributes
    document.querySelectorAll('[data-i18n-title]').forEach(el => {
      const key = el.dataset.i18nTitle;
      const translated = t(key);
      if (translated !== key) {
        el.title = translated;
      }
    });

    // Translate placeholders
    document.querySelectorAll('[data-i18n-placeholder]').forEach(el => {
      const key = el.dataset.i18nPlaceholder;
      const translated = t(key);
      if (translated !== key) {
        el.placeholder = translated;
      }
    });

    // Update HTML lang attribute
    document.documentElement.lang = _currentLang;
  }

  /**
   * Update the flag toggle button UI
   */
  function _updateFlagUI() {
    const flagBtn = document.getElementById('btnLangToggle');
    if (flagBtn) {
      // Show flag of CURRENT language
      flagBtn.textContent = _currentLang === 'fr' ? '🇫🇷' : '🇬🇧';
      flagBtn.title = _currentLang === 'fr' ? 'Switch to English' : 'Passer en Français';
    }
  }

  /**
   * Get current language code
   * @returns {string} Current language ('fr' or 'en')
   */
  function getLang() {
    return _currentLang;
  }

  // Public API
  return {
    init,
    t,
    setLanguage,
    toggleLanguage,
    applyTranslations,
    getLang
  };
})();

// Global shortcut for t() function - makes it easy to use everywhere
function t(key, params) {
  return I18n.t(key, params);
}

console.debug('✅ i18n.js loaded');
