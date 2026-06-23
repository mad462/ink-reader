const fs = require('fs');
const CREngine = require('../tools/epub-to-xtc-converter/web/crengine.js');

const epubPath = 'D:\\Desktop\\电子书相关\\哈代作品集..epub';
const fontPath = 'D:\\FUCKIDF\\ink-reader\\tools\\epub-to-xtc-converter\\fonts\\致一宋體_屛幕閱讀版-Regular.ttf';

CREngine().then(function(Module) {
    const renderer = new Module.EpubRenderer(480, 800);
    const data = fs.readFileSync(epubPath);
    const bytes = new Uint8Array(data);
    const fontData = fs.readFileSync(fontPath);
    const fontBytes = new Uint8Array(fontData);
    const ptr = Module.allocateMemory(bytes.length);
    const fontPtr = Module.allocateMemory(fontBytes.length);
    Module.HEAPU8.set(bytes, ptr);
    Module.HEAPU8.set(fontBytes, fontPtr);
    console.log('REGISTER_FONT', renderer.registerFontFromMemory(fontPtr, fontBytes.length, '致一宋體_屛幕閱讀版-Regular.ttf'));
    renderer.loadEpubFromMemory(ptr, bytes.length);
    renderer.setMargins(16, 16, 16, 16);
    renderer.setFontSize(34);
    renderer.setInterlineSpace(120);
    renderer.setFontFace('致一宋體_屛幕閱讀版-Regular');
    renderer.setTextAlign(3);
    console.log('PAGE_COUNT', renderer.getPageCount());
    renderer.renderCurrentPage();

    const probes = [
        '还乡',
        '第一卷 三女性',
        '四 卡子路上的停顿',
        '卡子路',
        '她们两个，一直往下走'
    ];

    for (const query of probes) {
        try {
            const result = renderer.search(query, false);
            console.log('SEARCH', query, JSON.stringify(result));
            if (result && result.length > 0) {
                try {
                    renderer.goToSearchResult(0);
                    console.log('AFTER_GO_TO_SEARCH_RESULT', query, renderer.getCurrentPage(), renderer.getPositionPercent());
                } catch (jumpErr) {
                    console.log('GO_TO_SEARCH_RESULT_ERR', query, jumpErr && jumpErr.message ? jumpErr.message : String(jumpErr));
                }
            }
        } catch (err) {
            console.log('SEARCH_ERR', query, err && err.message ? err.message : String(err));
        }
    }

    try {
        for (let page = 0; page < 8; page++) {
            renderer.goToPage(page);
            renderer.renderCurrentPage();
            console.log('PAGE_TEXT', page, JSON.stringify(renderer.getPageText()));
        }
    } catch (err) {
        console.log('PAGE_TEXT_ERR', err && err.message ? err.message : String(err));
    }

    Module.freeMemory(fontPtr);
    Module.freeMemory(ptr);
}).catch(function(err) {
    console.error(err);
    process.exit(1);
});
