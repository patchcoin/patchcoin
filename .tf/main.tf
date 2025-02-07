variable "hcloud_token" {
  type      = string
  sensitive = true
}

variable "github_pat" {
  type      = string
  sensitive = true
}

variable "github_owner" {
  type = string
}

variable "github_repo" {
  type = string
}

variable "runner_labels" {
  type    = list(string)
  default = ["hetzner"]
}

variable "server_name" {
  type    = string
  default = "runner"
}

variable "runner_count" {
  type    = number
  default = 1
}

variable "run_id" {
  type = number
}

variable "existing_ssh_key_name" {
  description = "Name of the existing SSH key on Hetzner that should be used."
  type        = string
  default     = "runner-key"
}

variable "runner_public_key" {
  description = "The public SSH key corresponding to the private key (not used if using an existing key)."
  type        = string
}

terraform {
  required_providers {
    hcloud = {
      source  = "hetznercloud/hcloud"
      version = "~> 1.49.1"
    }
  }
  required_version = ">= 1.0.0"
}

provider "hcloud" {
  token = var.hcloud_token
}

locals {
  runner_version = "2.322.0"
}

data "http" "github_runner_token" {
  url    = "https://api.github.com/repos/${var.github_owner}/${var.github_repo}/actions/runners/registration-token"
  method = "POST"

  request_headers = {
    "Authorization" = "Bearer ${var.github_pat}"
    "Accept"        = "application/vnd.github.v3+json"
  }
}

locals {
  ephemeral_runner_token = jsondecode(data.http.github_runner_token.response_body).token
}

data "hcloud_ssh_key" "runner_key" {
  name = var.existing_ssh_key_name
}

resource "hcloud_server" "runner" {
  count       = var.runner_count
  name        = "${var.server_name}-${var.run_id}-${count.index}"
  server_type = "cpx51"
  image       = "ubuntu-24.04"
  ssh_keys    = [data.hcloud_ssh_key.runner_key.id]

  user_data = <<-CLOUDINIT
    #cloud-config

    write_files:
      - path: /home/runner/cleanup.sh
        permissions: '0755'
        content: |
          #!/bin/bash
          cd /home/runner
          if [ -f .service ]; then
            sudo ./svc.sh stop || true
            sudo ./svc.sh uninstall || true
          fi
          if [ -f .runner ]; then
            sudo -u runner ./config.sh remove --token "${local.ephemeral_runner_token}" || true
          fi

    runcmd:
      - |
        set -eux
        curl -fsSL https://get.docker.com -o get-docker.sh
        sh get-docker.sh
        systemctl enable --now docker

        useradd -m runner
        usermod -aG docker runner

        GITHUB_OWNER="${var.github_owner}"
        GITHUB_REPO="${var.github_repo}"
        RUNNER_TOKEN="${local.ephemeral_runner_token}"
        RUNNER_LABELS="${join(",", var.runner_labels)}"
        RUNNER_VERSION="${local.runner_version}"

        cd /home/runner
        curl -o actions-runner-linux-x64-${local.runner_version}.tar.gz -L \
          https://github.com/actions/runner/releases/download/v${local.runner_version}/actions-runner-linux-x64-${local.runner_version}.tar.gz
        tar xzf actions-runner-linux-x64-${local.runner_version}.tar.gz
        chown -R runner:runner /home/runner

        /home/runner/bin/installdependencies.sh

        echo "runner ALL=(ALL) NOPASSWD: ALL" | sudo tee -a /etc/sudoers
        sudo -u runner env RUNNER_ALLOW_RUNASROOT=1 ./config.sh \
          --url "https://github.com/${var.github_owner}/${var.github_repo}" \
          --token "${local.ephemeral_runner_token}" \
          --labels "${join(",", var.runner_labels)}" \
          --unattended --replace

        ./svc.sh install runner
        ./svc.sh start
  CLOUDINIT
}

resource "null_resource" "runner_cleanup" {
  count = var.runner_count

  triggers = {
    server_ip = hcloud_server.runner[count.index].ipv4_address
  }

  depends_on = [hcloud_server.runner]

  provisioner "remote-exec" {
    when = destroy

    connection {
      host = self.triggers.server_ip
      type = "ssh"
      user = "root"
      private_key = file("./runner_private_key.pem")
    }

    inline = [
      "bash /home/runner/cleanup.sh || true"
    ]
  }
}
