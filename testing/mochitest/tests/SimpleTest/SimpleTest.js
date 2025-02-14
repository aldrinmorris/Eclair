/**
 * SimpleTest, a partial Test.Simple/Test.More API compatible test library.
 *
 * Why?
 *
 * Test.Simple doesn't work on IE < 6.
 * TODO:
 *  * Support the Test.Simple API used by MochiKit, to be able to test MochiKit
 * itself against IE 5.5
 *
 * NOTE: Pay attention to cross-browser compatibility in this file. For
 * instance, do not use const or JS > 1.5 features which are not yet
 * implemented everywhere.
 *
**/

var SimpleTest = { };

var parentRunner = null;
if (parent) {
    parentRunner = parent.TestRunner;
    if (!parentRunner && parent.wrappedJSObject) {
        parentRunner = parent.wrappedJSObject.TestRunner;
    }

    // This is the case where we have ChromePowers in harness.xul and we need it in the iframe
    if (window.SpecialPowers == undefined && parent.SpecialPowers !== undefined) {
        window.SpecialPowers = parent.SpecialPowers;
    }
}

// Workaround test_focus.xul where we open a window which opens another window which includes SimpleTest.js
if (window.SpecialPowers == undefined && window.opener && window.opener.SpecialPowers !== undefined) {
    window.SpecialPowers = window.opener.SpecialPowers;
}

/* Helper functions pulled out of various MochiKit modules */
if (typeof(repr) == 'undefined') {
    function repr(o) {
        if (typeof(o) == "undefined") {
            return "undefined";
        } else if (o === null) {
            return "null";
        }
        try {
            if (typeof(o.__repr__) == 'function') {
                return o.__repr__();
            } else if (typeof(o.repr) == 'function' && o.repr != arguments.callee) {
                return o.repr();
            }
       } catch (e) {
       }
       try {
            if (typeof(o.NAME) == 'string' && (
                    o.toString == Function.prototype.toString ||
                    o.toString == Object.prototype.toString
                )) {
                return o.NAME;
            }
        } catch (e) {
        }
        try {
            var ostring = (o + "");
        } catch (e) {
            return "[" + typeof(o) + "]";
        }
        if (typeof(o) == "function") {
            o = ostring.replace(/^\s+/, "");
            var idx = o.indexOf("{");
            if (idx != -1) {
                o = o.substr(0, idx) + "{...}";
            }
        }
        return ostring;
    };
} 

/* This returns a function that applies the previously given parameters.
 * This is used by SimpleTest.showReport
 */
if (typeof(partial) == 'undefined') {
    function partial(func) {
        var args = [];
        for (var i = 1; i < arguments.length; i++) {
            args.push(arguments[i]);
        }
        return function() {
            if (arguments.length > 0) {
                for (var i = 1; i < arguments.length; i++) {
                    args.push(arguments[i]);
                }
            }
            func(args);
        };
    };
}

if (typeof(getElement) == 'undefined') {
    function getElement(id) {
        return ((typeof(id) == "string") ?
            document.getElementById(id) : id); 
    };
    this.$ = this.getElement;
}

SimpleTest._newCallStack = function(path) {
    var rval = function () {
        var callStack = arguments.callee.callStack;
        for (var i = 0; i < callStack.length; i++) {
            if (callStack[i].apply(this, arguments) === false) {
                break;
            }
        }
        try {
            this[path] = null;
        } catch (e) {
            // pass
        }
    };
    rval.callStack = [];
    return rval;
};

if (typeof(addLoadEvent) == 'undefined') {
    function addLoadEvent(func) {
        var existing = window["onload"];
        var regfunc = existing;
        if (!(typeof(existing) == 'function'
                && typeof(existing.callStack) == "object"
                && existing.callStack !== null)) {
            regfunc = SimpleTest._newCallStack("onload");
            if (typeof(existing) == 'function') {
                regfunc.callStack.push(existing);
            }
            window["onload"] = regfunc;
        }
        regfunc.callStack.push(func);
    };
}

function createEl(type, attrs, html) {
    //use createElementNS so the xul/xhtml tests have no issues
    var el;
    if (!document.body) {
        el = document.createElementNS("http://www.w3.org/1999/xhtml", type);
    }
    else {
        el = document.createElement(type);
    }
    if (attrs !== null && attrs !== undefined) {
        for (var k in attrs) {
            el.setAttribute(k, attrs[k]);
        }
    }
    if (html !== null && html !== undefined) {
        el.appendChild(document.createTextNode(html));
    }
    return el;
}

/* lots of tests use this as a helper to get css properties */
if (typeof(computedStyle) == 'undefined') {
    function computedStyle(elem, cssProperty) {
        elem = getElement(elem);
        if (elem.currentStyle) {
            return elem.currentStyle[cssProperty];
        }
        if (typeof(document.defaultView) == 'undefined' || document === null) {
            return undefined;
        }
        var style = document.defaultView.getComputedStyle(elem, null);
        if (typeof(style) == 'undefined' || style === null) {
            return undefined;
        }
        
        var selectorCase = cssProperty.replace(/([A-Z])/g, '-$1'
            ).toLowerCase();
            
        return style.getPropertyValue(selectorCase);
    };
}

/**
 * Check for OOP test plugin
**/
SimpleTest.testPluginIsOOP = function () {
    var testPluginIsOOP = false;
    if (navigator.platform.indexOf("Mac") == 0) {
        if (SpecialPowers.XPCOMABI.match(/x86-/)) {
            try {
                testPluginIsOOP = SpecialPowers.getBoolPref("dom.ipc.plugins.enabled.i386.test.plugin");
            } catch (e) {
                testPluginIsOOP = SpecialPowers.getBoolPref("dom.ipc.plugins.enabled.i386");
            }
        }
        else if (SpecialPowers.XPCOMABI.match(/x86_64-/)) {
            try {
                testPluginIsOOP = SpecialPowers.getBoolPref("dom.ipc.plugins.enabled.x86_64.test.plugin");
            } catch (e) {
                testPluginIsOOP = SpecialPowers.getBoolPref("dom.ipc.plugins.enabled.x86_64");
            }
        }
    }
    else {
        testPluginIsOOP = SpecialPowers.getBoolPref("dom.ipc.plugins.enabled");
    }

    return testPluginIsOOP;
};

SimpleTest._tests = [];
SimpleTest._stopOnLoad = true;

/**
 * Something like assert.
**/
SimpleTest.ok = function (condition, name, diag) {
    var test = {'result': !!condition, 'name': name, 'diag': diag};
    SimpleTest._logResult(test, "TEST-PASS", "TEST-UNEXPECTED-FAIL");
    SimpleTest._tests.push(test);
};

/**
 * Roughly equivalent to ok(a==b, name)
**/
SimpleTest.is = function (a, b, name) {
    var pass = (a == b);
    var diag = pass ? repr(a) + " should equal " + repr(b)
                    : "got " + repr(a) + ", expected " + repr(b)
    SimpleTest.ok(pass, name, diag);
};

SimpleTest.isnot = function (a, b, name) {
    var pass = (a != b);
    var diag = pass ? repr(a) + " should not equal " + repr(b)
                    : "didn't expect " + repr(a) + ", but got it";
    SimpleTest.ok(pass, name, diag);
};

//  --------------- Test.Builder/Test.More todo() -----------------

SimpleTest.todo = function(condition, name, diag) {
    var test = {'result': !!condition, 'name': name, 'diag': diag, todo: true};
    SimpleTest._logResult(test, "TEST-UNEXPECTED-PASS", "TEST-KNOWN-FAIL");
    SimpleTest._tests.push(test);
};

SimpleTest._getCurrentTestURL = function() {
    return parentRunner && parentRunner.currentTestURL ||
           typeof gTestPath == "string" && gTestPath ||
           "unknown test url";
};

SimpleTest._logResult = function(test, passString, failString) {
    var isError = !test.result == !test.todo;
    var resultString = test.result ? passString : failString;
    var url = SimpleTest._getCurrentTestURL();
    var diagnostic = test.name + (test.diag ? " - " + test.diag : "");
    var msg = [resultString, url, diagnostic].join(" | ");
    if (parentRunner) {
        if (isError) {
            parentRunner.error(msg);
        } else {
            parentRunner.log(msg);
        }
    } else {
        dump(msg + "\n");
    }
};

SimpleTest.info = function(name, message) {
    this._logResult({result:true, name:name, diag:message}, "TEST-INFO");
};

/**
 * Copies of is and isnot with the call to ok replaced by a call to todo.
**/

SimpleTest.todo_is = function (a, b, name) {
    var pass = (a == b);
    var diag = pass ? repr(a) + " should equal " + repr(b)
                    : "got " + repr(a) + ", expected " + repr(b);
    SimpleTest.todo(pass, name, diag);
};

SimpleTest.todo_isnot = function (a, b, name) {
    var pass = (a != b);
    var diag = pass ? repr(a) + " should not equal " + repr(b)
                    : "didn't expect " + repr(a) + ", but got it";
    SimpleTest.todo(pass, name, diag);
};


/**
 * Makes a test report, returns it as a DIV element.
**/
SimpleTest.report = function () {
    var passed = 0;
    var failed = 0;
    var todo = 0;

    // Report tests which did not actually check anything.
    if (SimpleTest._tests.length == 0)
      // ToDo: Do s/todo/ok/ when all the tests are fixed. (Bug 483407)
      SimpleTest.todo(false, "[SimpleTest.report()] No checks actually run.");

    var tallyAndCreateDiv = function (test) {
            var cls, msg, div;
            var diag = test.diag ? " - " + test.diag : "";
            if (test.todo && !test.result) {
                todo++;
                cls = "test_todo";
                msg = "todo | " + test.name + diag;
            } else if (test.result && !test.todo) {
                passed++;
                cls = "test_ok";
                msg = "passed | " + test.name + diag;
            } else {
                failed++;
                cls = "test_not_ok";
                msg = "failed | " + test.name + diag;
            }
          div = createEl('div', {'class': cls}, msg);
          return div;
        };
    var results = [];
    for (var d=0; d<SimpleTest._tests.length; d++) {
        results.push(tallyAndCreateDiv(SimpleTest._tests[d]));
    }

    var summary_class = failed != 0 ? 'some_fail' :
                          passed == 0 ? 'todo_only' : 'all_pass';

    var div1 = createEl('div', {'class': 'tests_report'});
    var div2 = createEl('div', {'class': 'tests_summary ' + summary_class});
    var div3 = createEl('div', {'class': 'tests_passed'}, 'Passed: ' + passed);
    var div4 = createEl('div', {'class': 'tests_failed'}, 'Failed: ' + failed);
    var div5 = createEl('div', {'class': 'tests_todo'}, 'Todo: ' + todo);
    div2.appendChild(div3);
    div2.appendChild(div4);
    div2.appendChild(div5);
    div1.appendChild(div2);
    for (var t=0; t<results.length; t++) {
        //iterate in order
        div1.appendChild(results[t]);
    }
    return div1;
};

/**
 * Toggle element visibility
**/
SimpleTest.toggle = function(el) {
    if (computedStyle(el, 'display') == 'block') {
        el.style.display = 'none';
    } else {
        el.style.display = 'block';
    }
};

/**
 * Toggle visibility for divs with a specific class.
**/
SimpleTest.toggleByClass = function (cls, evt) {
    var children = document.getElementsByTagName('div');
    var elements = [];
    for (var i=0; i<children.length; i++) {
        var child = children[i];
        var clsName = child.className;
        if (!clsName) {
            continue;
        }    
        var classNames = clsName.split(' ');
        for (var j = 0; j < classNames.length; j++) {
            if (classNames[j] == cls) {
                elements.push(child);
                break;
            }    
        }    
    }
    for (var t=0; t<elements.length; t++) {
        //TODO: again, for-in loop over elems seems to break this
        SimpleTest.toggle(elements[t]);
    }
    if (evt)
        evt.preventDefault();
};

/**
 * Shows the report in the browser
**/
SimpleTest.showReport = function() {
    var togglePassed = createEl('a', {'href': '#'}, "Toggle passed checks");
    var toggleFailed = createEl('a', {'href': '#'}, "Toggle failed checks");
    var toggleTodo = createEl('a',{'href': '#'}, "Toggle todo checks");
    togglePassed.onclick = partial(SimpleTest.toggleByClass, 'test_ok');
    toggleFailed.onclick = partial(SimpleTest.toggleByClass, 'test_not_ok');
    toggleTodo.onclick = partial(SimpleTest.toggleByClass, 'test_todo');
    var body = document.body;  // Handles HTML documents
    if (!body) {
        // Do the XML thing.
        body = document.getElementsByTagNameNS("http://www.w3.org/1999/xhtml",
                                               "body")[0];
    }
    var firstChild = body.childNodes[0];
    var addNode;
    if (firstChild) {
        addNode = function (el) {
            body.insertBefore(el, firstChild);
        };
    } else {
        addNode = function (el) {
            body.appendChild(el)
        };
    }
    addNode(togglePassed);
    addNode(createEl('span', null, " "));
    addNode(toggleFailed);
    addNode(createEl('span', null, " "));
    addNode(toggleTodo);
    addNode(SimpleTest.report());
    // Add a separator from the test content.
    addNode(createEl('hr'));
};

/**
 * Tells SimpleTest to don't finish the test when the document is loaded,
 * useful for asynchronous tests.
 *
 * When SimpleTest.waitForExplicitFinish is called,
 * explicit SimpleTest.finish() is required.
**/
SimpleTest.waitForExplicitFinish = function () {
    SimpleTest._stopOnLoad = false;
};

/**
 * Multiply the timeout the parent runner uses for this test by the
 * given factor.
 *
 * For example, in a test that may take a long time to complete, using
 * "SimpleTest.requestLongerTimeout(5)" will give it 5 times as long to
 * finish.
 */
SimpleTest.requestLongerTimeout = function (factor) {
    if (parentRunner) {
        parentRunner.requestLongerTimeout(factor);
    }
}

SimpleTest.waitForFocus_started = false;
SimpleTest.waitForFocus_loaded = false;
SimpleTest.waitForFocus_focused = false;

/**
 * If the page is not yet loaded, waits for the load event. In addition, if
 * the page is not yet focused, focuses and waits for the window to be
 * focused. Calls the callback when completed. If the current page is
 * 'about:blank', then the page is assumed to not yet be loaded. Pass true for
 * expectBlankPage to not make this assumption if you expect a blank page to
 * be present.
 *
 * targetWindow should be specified if it is different than 'window'. The actual
 * focused window may be a descendant of targetWindow.
 *
 * @param callback
 *        function called when load and focus are complete
 * @param targetWindow
 *        optional window to be loaded and focused, defaults to 'window'
 * @param expectBlankPage
 *        true if targetWindow.location is 'about:blank'. Defaults to false
 */
SimpleTest.waitForFocus = function (callback, targetWindow, expectBlankPage) {
    if (!targetWindow)
      targetWindow = window;

    SimpleTest.waitForFocus_started = false;
    expectBlankPage = !!expectBlankPage;

    var childTargetWindow = {};
    SpecialPowers.getFocusedElementForWindow(targetWindow, true, childTargetWindow);
    childTargetWindow = childTargetWindow.value;

    function info(msg) {
        SimpleTest.info(msg);
    }
    function getHref(aWindow) {
      return SpecialPowers.getPrivilegedProps(aWindow, 'location.href');
    }

    function maybeRunTests() {
        if (SimpleTest.waitForFocus_loaded &&
            SimpleTest.waitForFocus_focused &&
            !SimpleTest.waitForFocus_started) {
            SimpleTest.waitForFocus_started = true;
            setTimeout(callback, 0, targetWindow);
        }
    }

    function waitForEvent(event) {
        try {
            // Check to make sure that this isn't a load event for a blank or
            // non-blank page that wasn't desired.
            if (event.type == "load" && (expectBlankPage != (event.target.location == "about:blank")))
                return;

            SimpleTest["waitForFocus_" + event.type + "ed"] = true;
            var win = (event.type == "load") ? targetWindow : childTargetWindow;
            win.removeEventListener(event.type, waitForEvent, true);
            maybeRunTests();
        } catch (e) {
            SimpleTest.ok(false, "Exception caught in waitForEvent: " + e.message +
                ", at: " + e.fileName + " (" + e.lineNumber + ")");
        }
    }

    // If the current document is about:blank and we are not expecting a blank
    // page (or vice versa), and the document has not yet loaded, wait for the
    // page to load. A common situation is to wait for a newly opened window
    // to load its content, and we want to skip over any intermediate blank
    // pages that load. This issue is described in bug 554873.
    SimpleTest.waitForFocus_loaded =
        (expectBlankPage == (getHref(targetWindow) == "about:blank")) &&
        targetWindow.document.readyState == "complete";
    if (!SimpleTest.waitForFocus_loaded) {
        info("must wait for load");
        targetWindow.addEventListener("load", waitForEvent, true);
    }

    // Check if the desired window is already focused.
    var focusedChildWindow = { };
    if (SpecialPowers.activeWindow()) {
        SpecialPowers.getFocusedElementForWindow(SpecialPowers.activeWindow(), true, focusedChildWindow);
        focusedChildWindow = focusedChildWindow.value;
    }

    // If this is a child frame, ensure that the frame is focused.
    SimpleTest.waitForFocus_focused = (focusedChildWindow == childTargetWindow);
    if (SimpleTest.waitForFocus_focused) {
        info("already focused");
        // If the frame is already focused and loaded, call the callback directly.
        maybeRunTests();
    }
    else {
        info("must wait for focus");
        childTargetWindow.addEventListener("focus", waitForEvent, true);
        SpecialPowers.focus(childTargetWindow);
    }
};

SimpleTest.waitForClipboard_polls = 0;

/*
 * Polls the clipboard waiting for the expected value. A known value different than
 * the expected value is put on the clipboard first (and also polled for) so we
 * can be sure the value we get isn't just the expected value because it was already
 * on the clipboard. This only uses the global clipboard and only for text/unicode
 * values.
 *
 * @param aExpectedStringOrValidatorFn
 *        The string value that is expected to be on the clipboard or a
 *        validator function getting cripboard data and returning a bool.
 * @param aSetupFn
 *        A function responsible for setting the clipboard to the expected value,
 *        called after the known value setting succeeds.
 * @param aSuccessFn
 *        A function called when the expected value is found on the clipboard.
 * @param aFailureFn
 *        A function called if the expected value isn't found on the clipboard
 *        within 5s. It can also be called if the known value can't be found.
 * @param aFlavor [optional] The flavor to look for.  Defaults to "text/unicode".
 */
SimpleTest.__waitForClipboardMonotonicCounter = 0;
SimpleTest.__defineGetter__("_waitForClipboardMonotonicCounter", function () {
  return SimpleTest.__waitForClipboardMonotonicCounter++;
});
SimpleTest.waitForClipboard = function(aExpectedStringOrValidatorFn, aSetupFn,
                                       aSuccessFn, aFailureFn, aFlavor) {
    var requestedFlavor = aFlavor || "text/unicode";

    // Build a default validator function for common string input.
    var inputValidatorFn = typeof(aExpectedStringOrValidatorFn) == "string"
        ? function(aData) aData == aExpectedStringOrValidatorFn
        : aExpectedStringOrValidatorFn;

    // reset for the next use
    function reset() {
        SimpleTest.waitForClipboard_polls = 0;
    }

    function wait(validatorFn, successFn, failureFn, flavor) {
        if (++SimpleTest.waitForClipboard_polls > 50) {
            // Log the failure.
            SimpleTest.ok(false, "Timed out while polling clipboard for pasted data.");
            reset();
            failureFn();
            return;
        }

        data = SpecialPowers.getClipboardData(flavor);

        if (validatorFn(data)) {
            // Don't show the success message when waiting for preExpectedVal
            if (preExpectedVal)
                preExpectedVal = null;
            else
                SimpleTest.ok(true, "Clipboard has the correct value");
            reset();
            successFn();
        } else {
            setTimeout(function() wait(validatorFn, successFn, failureFn, flavor), 100);
        }
    }

    // First we wait for a known value different from the expected one.
    var preExpectedVal = SimpleTest._waitForClipboardMonotonicCounter +
                         "-waitForClipboard-known-value";
    SpecialPowers.clipboardCopyString(preExpectedVal);
    wait(function(aData) aData  == preExpectedVal,
         function() {
           // Call the original setup fn
           aSetupFn();
           wait(inputValidatorFn, aSuccessFn, aFailureFn, requestedFlavor);
         }, aFailureFn, "text/unicode");
}

/**
 * Executes a function shortly after the call, but lets the caller continue
 * working (or finish).
 */
SimpleTest.executeSoon = function(aFunc) {
    // Once SpecialPowers is available in chrome mochitests, we can replace the
    // body of this function with a call to SpecialPowers.executeSoon().
    if ("Components" in window && "classes" in window.Components) {
        try {
            netscape.security.PrivilegeManager
              .enablePrivilege("UniversalXPConnect");
            var tm = Components.classes["@mozilla.org/thread-manager;1"]
                       .getService(Components.interfaces.nsIThreadManager);

            tm.mainThread.dispatch({
                run: function() {
                    aFunc();
                }
            }, Components.interfaces.nsIThread.DISPATCH_NORMAL);
            return;
        } catch (ex) {
            // If the above fails (most likely because of enablePrivilege
            // failing), fall through to the setTimeout path.
        }
    }
    setTimeout(aFunc, 0);
}

/**
 * Finishes the tests. This is automatically called, except when
 * SimpleTest.waitForExplicitFinish() has been invoked.
**/
SimpleTest.finish = function () {
    if (SimpleTest._expectingUncaughtException) {
        SimpleTest.ok(false, "expectUncaughtException was called but no uncaught exception was detected!");
    }
    if (parentRunner) {
        /* We're running in an iframe, and the parent has a TestRunner */
        parentRunner.testFinished(SimpleTest._tests);
    } else {
        SimpleTest.showReport();
    }
};

/**
 * Indicates to the test framework that the current test expects one or
 * more crashes (from plugins or IPC documents), and that the minidumps from
 * those crashes should be removed.
 */
SimpleTest.expectChildProcessCrash = function () {
    if (parentRunner) {
        parentRunner.expectChildProcessCrash();
    }
};

/**
 * Indicates to the test framework that the next uncaught exception during
 * the test is expected, and should not cause a test failure.
 */
SimpleTest.expectUncaughtException = function () {
    SimpleTest._expectingUncaughtException = true;
};


addLoadEvent(function() {
    if (SimpleTest._stopOnLoad) {
        SimpleTest.finish();
    }
});

//  --------------- Test.Builder/Test.More isDeeply() -----------------


SimpleTest.DNE = {dne: 'Does not exist'};
SimpleTest.LF = "\r\n";
SimpleTest._isRef = function (object) {
    var type = typeof(object);
    return type == 'object' || type == 'function';
};


SimpleTest._deepCheck = function (e1, e2, stack, seen) {
    var ok = false;
    // Either they're both references or both not.
    var sameRef = !(!SimpleTest._isRef(e1) ^ !SimpleTest._isRef(e2));
    if (e1 == null && e2 == null) {
        ok = true;
    } else if (e1 != null ^ e2 != null) {
        ok = false;
    } else if (e1 == SimpleTest.DNE ^ e2 == SimpleTest.DNE) {
        ok = false;
    } else if (sameRef && e1 == e2) {
        // Handles primitives and any variables that reference the same
        // object, including functions.
        ok = true;
    } else if (SimpleTest.isa(e1, 'Array') && SimpleTest.isa(e2, 'Array')) {
        ok = SimpleTest._eqArray(e1, e2, stack, seen);
    } else if (typeof e1 == "object" && typeof e2 == "object") {
        ok = SimpleTest._eqAssoc(e1, e2, stack, seen);
    } else {
        // If we get here, they're not the same (function references must
        // always simply reference the same function).
        stack.push({ vals: [e1, e2] });
        ok = false;
    }
    return ok;
};

SimpleTest._eqArray = function (a1, a2, stack, seen) {
    // Return if they're the same object.
    if (a1 == a2) return true;

    // JavaScript objects have no unique identifiers, so we have to store
    // references to them all in an array, and then compare the references
    // directly. It's slow, but probably won't be much of an issue in
    // practice. Start by making a local copy of the array to as to avoid
    // confusing a reference seen more than once (such as [a, a]) for a
    // circular reference.
    for (var j = 0; j < seen.length; j++) {
        if (seen[j][0] == a1) {
            return seen[j][1] == a2;
        }
    }

    // If we get here, we haven't seen a1 before, so store it with reference
    // to a2.
    seen.push([ a1, a2 ]);

    var ok = true;
    // Only examines enumerable attributes. Only works for numeric arrays!
    // Associative arrays return 0. So call _eqAssoc() for them, instead.
    var max = a1.length > a2.length ? a1.length : a2.length;
    if (max == 0) return SimpleTest._eqAssoc(a1, a2, stack, seen);
    for (var i = 0; i < max; i++) {
        var e1 = i > a1.length - 1 ? SimpleTest.DNE : a1[i];
        var e2 = i > a2.length - 1 ? SimpleTest.DNE : a2[i];
        stack.push({ type: 'Array', idx: i, vals: [e1, e2] });
        ok = SimpleTest._deepCheck(e1, e2, stack, seen);
        if (ok) {
            stack.pop();
        } else {
            break;
        }
    }
    return ok;
};

SimpleTest._eqAssoc = function (o1, o2, stack, seen) {
    // Return if they're the same object.
    if (o1 == o2) return true;

    // JavaScript objects have no unique identifiers, so we have to store
    // references to them all in an array, and then compare the references
    // directly. It's slow, but probably won't be much of an issue in
    // practice. Start by making a local copy of the array to as to avoid
    // confusing a reference seen more than once (such as [a, a]) for a
    // circular reference.
    seen = seen.slice(0);
    for (var j = 0; j < seen.length; j++) {
        if (seen[j][0] == o1) {
            return seen[j][1] == o2;
        }
    }

    // If we get here, we haven't seen o1 before, so store it with reference
    // to o2.
    seen.push([ o1, o2 ]);

    // They should be of the same class.

    var ok = true;
    // Only examines enumerable attributes.
    var o1Size = 0; for (var i in o1) o1Size++;
    var o2Size = 0; for (var i in o2) o2Size++;
    var bigger = o1Size > o2Size ? o1 : o2;
    for (var i in bigger) {
        var e1 = o1[i] == undefined ? SimpleTest.DNE : o1[i];
        var e2 = o2[i] == undefined ? SimpleTest.DNE : o2[i];
        stack.push({ type: 'Object', idx: i, vals: [e1, e2] });
        ok = SimpleTest._deepCheck(e1, e2, stack, seen)
        if (ok) {
            stack.pop();
        } else {
            break;
        }
    }
    return ok;
};

SimpleTest._formatStack = function (stack) {
    var variable = '$Foo';
    for (var i = 0; i < stack.length; i++) {
        var entry = stack[i];
        var type = entry['type'];
        var idx = entry['idx'];
        if (idx != null) {
            if (/^\d+$/.test(idx)) {
                // Numeric array index.
                variable += '[' + idx + ']';
            } else {
                // Associative array index.
                idx = idx.replace("'", "\\'");
                variable += "['" + idx + "']";
            }
        }
    }

    var vals = stack[stack.length-1]['vals'].slice(0, 2);
    var vars = [
        variable.replace('$Foo',     'got'),
        variable.replace('$Foo',     'expected')
    ];

    var out = "Structures begin differing at:" + SimpleTest.LF;
    for (var i = 0; i < vals.length; i++) {
        var val = vals[i];
        if (val == null) {
            val = 'undefined';
        } else {
            val == SimpleTest.DNE ? "Does not exist" : "'" + val + "'";
        }
    }

    out += vars[0] + ' = ' + vals[0] + SimpleTest.LF;
    out += vars[1] + ' = ' + vals[1] + SimpleTest.LF;

    return '    ' + out;
};


SimpleTest.isDeeply = function (it, as, name) {
    var ok;
    // ^ is the XOR operator.
    if (SimpleTest._isRef(it) ^ SimpleTest._isRef(as)) {
        // One's a reference, one isn't.
        ok = false;
    } else if (!SimpleTest._isRef(it) && !SimpleTest._isRef(as)) {
        // Neither is an object.
        ok = SimpleTest.is(it, as, name);
    } else {
        // We have two objects. Do a deep comparison.
        var stack = [], seen = [];
        if ( SimpleTest._deepCheck(it, as, stack, seen)) {
            ok = SimpleTest.ok(true, name);
        } else {
            ok = SimpleTest.ok(false, name, SimpleTest._formatStack(stack));
        }
    }
    return ok;
};

SimpleTest.typeOf = function (object) {
    var c = Object.prototype.toString.apply(object);
    var name = c.substring(8, c.length - 1);
    if (name != 'Object') return name;
    // It may be a non-core class. Try to extract the class name from
    // the constructor function. This may not work in all implementations.
    if (/function ([^(\s]+)/.test(Function.toString.call(object.constructor))) {
        return RegExp.$1;
    }
    // No idea. :-(
    return name;
};

SimpleTest.isa = function (object, clas) {
    return SimpleTest.typeOf(object) == clas;
};

// Global symbols:
var ok = SimpleTest.ok;
var is = SimpleTest.is;
var isnot = SimpleTest.isnot;
var todo = SimpleTest.todo;
var todo_is = SimpleTest.todo_is;
var todo_isnot = SimpleTest.todo_isnot;
var isDeeply = SimpleTest.isDeeply;
var info = SimpleTest.info;

var gOldOnError = window.onerror;
window.onerror = function simpletestOnerror(errorMsg, url, lineNumber) {
    var funcIdentifier = "[SimpleTest/SimpleTest.js, window.onerror]";

    // Log the message.
    // XXX Chrome mochitests sometimes trigger this window.onerror handler,
    // but there are a number of uncaught JS exceptions from those tests
    // currently, so we can't log them as errors just yet.  For now, when
    // not in a plain mochitest, just dump it so that the error is visible but
    // doesn't cause a test failure.  See bug 652494.
    var message = "An error occurred: " + errorMsg + " at " + url + ":" + lineNumber;
    var href = SpecialPowers.getPrivilegedProps(window, 'location.href');
    var isPlainMochitest = href.substring(0,7) != "chrome:";
    var isExpected = !!SimpleTest._expectingUncaughtException;
    if (isPlainMochitest) {
        SimpleTest.ok(isExpected, funcIdentifier, message);
        SimpleTest._expectingUncaughtException = false;
    } else {
        SimpleTest.info(funcIdentifier + " " + message);
    }
    // There is no Components.stack.caller to log. (See bug 511888.)

    // Call previous handler.
    if (gOldOnError) {
        try {
            // Ignore return value: always run default handler.
            gOldOnError(errorMsg, url, lineNumber);
        } catch (e) {
            // Log the error.
            SimpleTest.info("Exception thrown by gOldOnError(): " + e);
            // Log its stack.
            if (e.stack) {
                SimpleTest.info("JavaScript error stack:\n" + e.stack);
            }
        }
    }

    if (!SimpleTest._stopOnLoad && !isExpected) {
        // Need to finish() manually here, yet let the test actually end first.
        SimpleTest.executeSoon(SimpleTest.finish);
    }
};
