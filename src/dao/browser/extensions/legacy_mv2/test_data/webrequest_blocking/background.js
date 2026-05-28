chrome.webRequest.onBeforeRequest.addListener(
  function (details) {
    return { cancel: true };
  },
  { urls: ["*://dao-mv2-blocked.test/*"] },
  ["blocking"]
);
