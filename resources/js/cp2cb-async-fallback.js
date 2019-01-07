function displayAlert(id, txt) {
  var alert = document.getElementById(id);
  document.getElementById(id).removeAttribute("hidden");
  alert.focus();
}

function toggleEditable(element) {
    element.contentEditable = !(element.contentEditable);
    element.readOnly = !(element.readOnly);
}

function isiOS() {
    var ua = navigator.userAgent;
    var regex = new RegExp("(iPhone|iPad|iPod)");
    return regex.test(ua);
}

function selectRangeiOS(txtArea) {
    var range = document.createRange();
    toggleEditable(txtArea);
    range.selectNodeContents(txtArea);
    var selection = window.getSelection();
    selection.removeAllRanges();
    selection.addRange(range);
    txtArea.setSelectionRange(0,1999); // should be enough for any url
    toggleEditable(txtArea);
}

function cp2cbFallback(txtArea) {
  if (isiOS()) {
    selectRangeiOS(txtArea);
  } else {
    txtArea.select();
  }
  try {
    var ret = document.execCommand("copy");
    var msg = ret ? "success" : "failure";
    console.log("[FALLBACK] Copy to clipboard: " + msg);
    if (ret) window.getSelection().removeAllRanges();
    displayAlert(msg);
    cpBtn.title = ret ? "Link copied to clipboard!" : "Something went wrong.";

  } catch (err) {
    console.error("[FALLBACK] Unable to copy: ", err);
  }
}

function cp2cbAsync(txtArea) {
  navigator.clipboard.writeText(txtArea.value).then(
    function() {
      console.log("[ASYNC] Copying to clipboard was successful!");
      displayAlert("success");
      cpBtn.title = "Link copied to clipboard!";
    },
    function(err) {
      console.error("[ASYNC] Unable to copy: ", err);
      console.log("[ASYNC] Trying fallback method instead.");
      cp2cbFallback(txtArea);
      return
    }
  );
}

var cpBtn = document.querySelector(".button-copy");

cpBtn.addEventListener("click", function(event) {
  var txtArea = document.querySelector(".textarea-url");
  if (navigator.clipboard) {
    cp2cbAsync(txtArea);
  } else {
    cp2cbFallback(txtArea);
  }
});
