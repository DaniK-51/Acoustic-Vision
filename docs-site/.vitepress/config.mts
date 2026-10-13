import { defineConfig } from 'vitepress'

export default defineConfig({
  lang: 'ru-RU',
  title: 'Acoustic-Vision',
  description:
    'Open-source инструмент акустического анализа помещений: сцена, источники, алгоритмы, поле звука',
  cleanUrls: true,
  lastUpdated: true,
  themeConfig: {
    nav: [
      { text: 'Начало', link: '/guide/getting-started' },
      { text: 'Справочник', link: '/reference/api' },
      { text: 'FAQ', link: '/faq' },
    ],
    sidebar: [
      {
        text: 'Обзор',
        items: [{ text: 'Acoustic-Vision', link: '/' }],
      },
      {
        text: 'Руководство',
        items: [
          { text: 'Быстрый старт', link: '/guide/getting-started' },
          { text: 'Концепции', link: '/guide/concepts' },
          { text: 'Редактор сцен', link: '/guide/scene-editor' },
          { text: 'Mesh', link: '/guide/mesh' },
          { text: 'Методы расчёта', link: '/guide/engines' },
          { text: 'Источники', link: '/guide/sources' },
          { text: 'Симуляция', link: '/guide/simulation' },
          { text: 'GUI', link: '/guide/gui' },
        ],
      },
      {
        text: 'Справочник',
        items: [
          { text: 'REST API и WebSocket', link: '/reference/api' },
          { text: 'av-cli', link: '/reference/cli' },
          { text: 'JSON Schema', link: '/reference/schema' },
        ],
      },
      {
        text: 'Прочее',
        items: [{ text: 'Частые вопросы', link: '/faq' }],
      },
    ],
    outline: { level: [2, 3], label: 'На этой странице' },
    docFooter: {
      prev: 'Предыдущая страница',
      next: 'Следующая страница',
    },
    lastUpdatedText: 'Обновлено',
    darkModeSwitchLabel: 'Тема',
    sidebarMenuLabel: 'Меню',
    returnToTopLabel: 'Наверх',
    search: {
      provider: 'local',
      options: {
        translations: {
          button: {
            buttonText: 'Поиск',
            buttonAriaLabel: 'Поиск',
          },
          modal: {
            noResultsText: 'Ничего не найдено',
            resetButtonTitle: 'Сбросить',
            footer: {
              selectText: 'выбрать',
              navigateText: 'перейти',
              closeText: 'закрыть',
            },
          },
        },
      },
    },
  },
})
