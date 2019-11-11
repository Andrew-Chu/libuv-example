#include <stdio.h>
#include <stdlib.h>

#include <curl/curl.h>
#include <uv.h>
// ------------------------------------------------------------------------------------------------
CURLM*     curl_handle;
uv_loop_t* loop;
uv_timer_t timeout;

typedef struct curl_context_s {
	uv_poll_t     poll_handle;
	curl_socket_t sockfd;
} curl_context_t;
// ------------------------------------------------------------------------------------------------
curl_context_t* create_curl_context(curl_socket_t sockfd);
void curl_close_cb(uv_handle_t* handle);
void destroy_curl_context(curl_context_t* context);
void add_download(const char* url, int num);
void curl_perform_cb(uv_poll_t* req, int status, int events);
void on_timeout_cb(uv_timer_t* req, int status);
void start_timeout(CURLM* multi, long timeout_ms, void* userp);
int  handle_socket(CURL* easy, curl_socket_t s, int action, void* userp, void* socketp);
// -------------------------------------------------------------------------------------------------
int main(int argc, char** argv) {
	if (argc <= 1)
		return 0;

	if (curl_global_init(CURL_GLOBAL_ALL)) {
		fprintf(stderr, "Could not init cURL\n");
		return 1;
	}

    loop = uv_default_loop();
	uv_timer_init(loop, &timeout);

	curl_handle = curl_multi_init();
	curl_multi_setopt(curl_handle, CURLMOPT_SOCKETFUNCTION, handle_socket);
	curl_multi_setopt(curl_handle, CURLMOPT_TIMERFUNCTION, start_timeout);

	while (argc-- > 1) {
		add_download(argv[argc], argc);
	}

	uv_run(loop, UV_RUN_DEFAULT);
	curl_multi_cleanup(curl_handle);
	return 0;
}
// --------------------------------------------------------------------------------------------------
curl_context_t* create_curl_context(curl_socket_t sockfd) {
	curl_context_t* context;

	context         = (curl_context_t*)malloc(sizeof *context);
	context->sockfd = sockfd;

	uv_poll_init_socket(loop, &context->poll_handle, sockfd);
	context->poll_handle.data = context;

	return context;
}

// curl close callback
void curl_close_cb(uv_handle_t* handle) {
	curl_context_t* context = (curl_context_t*)handle->data;
	free(context);
}

void destroy_curl_context(curl_context_t* context) {
	uv_close((uv_handle_t*)&context->poll_handle, curl_close_cb);
}

// add download from url
void add_download(const char* url, int num) {
	char filename[50];
	sprintf(filename, "%d.txt", num);
	FILE* file = fopen(filename, "w");

	if (file == NULL) {
		fprintf(stderr, "Error opening %s\n", filename);
		return;
	}

	CURL* handle = curl_easy_init();
	curl_easy_setopt(handle, CURLOPT_WRITEDATA, file); // set options for a curl easy handle
	curl_easy_setopt(handle, CURLOPT_URL, url);
	curl_multi_add_handle(curl_handle, handle); // add an easy handle to a multi session
	fprintf(stderr, "Added download %s -> %s\n", url, filename);
}

// curl perform callback
void curl_perform_cb(uv_poll_t* req, int status, int events) {
	uv_timer_stop(&timeout);
	
    int running_handles;
	int flags = 0;

	if (events & UV_READABLE)
		flags |= CURL_CSELECT_IN; // bitwise or assignment
	if (events & UV_WRITABLE)
		flags |= CURL_CSELECT_OUT;

	curl_context_t* context = (curl_context_t*)req;

	// reads/writes available data given an action
	curl_multi_socket_action(curl_handle, context->sockfd, flags, &running_handles);

	char*    done_url;
	CURLMsg* message;
	int      pending;

	while ((message = curl_multi_info_read(curl_handle, &pending))) {
		switch (message->msg) {
			case CURLMSG_DONE:
				// extract information from a curl handle
				curl_easy_getinfo(message->easy_handle, CURLINFO_EFFECTIVE_URL, &done_url);
				printf("%s DONE\n", done_url);

				// remove an easy handle from a multi session
				curl_multi_remove_handle(curl_handle, message->easy_handle);
				curl_easy_cleanup(message->easy_handle); // End a libcurl easy handle

				break;
			default:
                fprintf(stderr, "CURLMSG default\n");
				abort(); // Aborts the current process, producing an abnormal termination.
		}
	}
}

// on timeout callback
void on_timeout_cb(uv_timer_t* req, int status) {
	int running_handles;
	// reads/writes available data given an action
	curl_multi_socket_action(curl_handle, CURL_SOCKET_TIMEOUT, 0, &running_handles);
}

void start_timeout(CURLM* multi, long timeout_ms, void* userp) {
	if (timeout_ms <= 0)
		timeout_ms = 1; // 0 means directly call socket_action, but we'll do it in a bit
	uv_timer_start(&timeout, on_timeout_cb, timeout_ms, 0);
}

int handle_socket(CURL* easy, curl_socket_t s, int action, void* userp, void* socketp) {
	curl_context_t* curl_context;

	if (action == CURL_POLL_IN || action == CURL_POLL_OUT) {
		if (socketp) {// CURL_POLL_OUT
			curl_context = (curl_context_t*)socketp;
		}
		else {// CURL_POLL_IN
			curl_context = create_curl_context(s);
		}
		curl_multi_assign(curl_handle, s, (void*)curl_context);
	}

	switch (action) {
		case CURL_POLL_IN:
			uv_poll_start(&curl_context->poll_handle, UV_READABLE, curl_perform_cb);
			break;
		case CURL_POLL_OUT:
			uv_poll_start(&curl_context->poll_handle, UV_WRITABLE, curl_perform_cb);
			break;
		case CURL_POLL_REMOVE:
			if (socketp) {
				uv_poll_stop(&((curl_context_t*)socketp)->poll_handle);
				destroy_curl_context((curl_context_t*)socketp);
				curl_multi_assign(curl_handle, s, NULL);
			}
			break;
		default:
            abort(); // Aborts the current process, producing an abnormal termination.
	}

	return 0;
}
