const { chromium } = require('playwright-core');
(async () => {
  const browser = await chromium.launch({
    headless: true,
    executablePath: 'C:/Program Files/Google/Chrome/Application/chrome.exe'
  });
  const page = await browser.newPage();
  const logs = [];
  page.on('console', msg => logs.push(`[${msg.type()}] ${msg.text()}`));
  page.on('pageerror', err => logs.push(`[pageerror] ${err.message}`));
  await page.goto('http://127.0.0.1:8000/web/', { waitUntil: 'domcontentloaded' });
  await page.waitForTimeout(4000);
  const options = await page.$$eval('#fontFamily option', opts => opts.map(o => ({ value: o.value, text: o.textContent })));
  const projectOptions = options.filter(o => o.text.includes('项目字体') || /[\u4e00-\u9fff]/.test(o.text));
  console.log('OPTIONS_JSON_START');
  console.log(JSON.stringify(projectOptions, null, 2));
  console.log('OPTIONS_JSON_END');
  console.log('LOGS_START');
  console.log(logs.join('\n'));
  console.log('LOGS_END');
  await browser.close();
})().catch(err => { console.error(err); process.exit(1); });
