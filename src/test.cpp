void Server::run(HttpParser &parser)
{
    std::vector<ClientConnection> clients;

    while (true)
    {
        std::vector<struct pollfd> fds;

        // 1. Add every listening socket to poll.
        for (std::size_t i = 0; i < _server_fds.size(); ++i)
        {
            struct pollfd server_poll;

            server_poll.fd = _server_fds[i];
            server_poll.events = POLLIN;
            server_poll.revents = 0;

            fds.push_back(server_poll);
        }

        // 2. Add all connected clients.
        for (std::size_t i = 0; i < clients.size(); ++i)
        {
            struct pollfd client_poll;

            client_poll.fd = clients[i].fd;
            client_poll.events =
                clients[i].response_ready ? POLLOUT : POLLIN;
            client_poll.revents = 0;

            fds.push_back(client_poll);
        }

        int result = poll(&fds[0], fds.size(), -1);

        if (result == -1)
        {
            if (errno == EINTR)
                continue;

            throw std::runtime_error("poll failed");
        }

        // 3. Handle every listening socket that is ready.
        for (std::size_t i = 0; i < _server_fds.size(); ++i)
        {
            if (fds[i].revents & POLLIN)
            {
                int client_fd = accept_connection(_server_fds[i]);

                if (client_fd != -1)
                    clients.push_back(ClientConnection(client_fd));
            }

            if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
                throw std::runtime_error("Listening socket error");
        }

        // 4. Handle client events.
        for (std::size_t i = _server_fds.size(); i < fds.size(); ++i)
        {
            int client_fd = fds[i].fd;
            short events = fds[i].revents;

            if (events == 0)
                continue;

            std::vector<ClientConnection>::iterator it = clients.begin();

            while (it != clients.end() && it->fd != client_fd)
                ++it;

            if (it == clients.end())
                continue;

            if (events & (POLLNVAL | POLLHUP | POLLERR))
            {
                remove_client(client_fd, clients);
                continue;
            }

            bool keep_client = true;

            if (events & POLLIN)
                keep_client = handle_client_event(*it, parser);

            if (keep_client && (events & POLLOUT))
                keep_client = handle_client_write(*it);

            if (!keep_client)
                remove_client(client_fd, clients);
        }
    }
}